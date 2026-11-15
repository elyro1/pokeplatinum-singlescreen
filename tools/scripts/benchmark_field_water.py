#!/usr/bin/env python3
"""Controlled Linux in-game water on/off CPU/GPU timings using a copied save."""
import argparse
import csv
import json
import os
from pathlib import Path
import shlex
import shutil
import statistics
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,default=ROOT/'build_linux')
    parser.add_argument('--baseline',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--scales',type=int,nargs='+',default=[2,3,10])
    args=parser.parse_args();build=args.build.resolve()
    commands=json.loads((build/'compile_commands.json').read_text())
    entry=next(x for x in commands if x['file'].endswith('/port/visuals/field_visuals.c'))
    flags=shlex.split(entry['command']);flags=flags[:flags.index('-MD')]
    i=flags.index('-include');del flags[i:i+2];flags.remove('-fpch-preprocess')
    flags+=['-D_GNU_SOURCE','-include',str(ROOT/'include/pch/global_pch.h')]
    results=[]
    with tempfile.TemporaryDirectory(prefix='platinum-water-bench-') as directory:
        temp=Path(directory);runtime=temp/'game';shutil.copytree(build/'pokeplatinum',runtime)
        for label,binary in [('before',args.baseline),('after',build/'main')]:
            if binary is None:continue
            binary=binary.resolve()
            symbols={parts[2]:int(parts[0],16) for line in subprocess.check_output(['nm','-n',str(binary)],text=True).splitlines() if len(parts:=line.split())==3 and parts[1]=='T'}
            required={'FIELD':'DEBUG_GetFieldSystem','TASK':'FieldSystem_IsRunningTask','WARP':'GUI_MapJump_JumpToMap','CONFIG':'SIM_Config_prj_GetConfig'}
            metrics={'PROFILE':'SIM_SetWaterProfiling','CPU':'FieldVisualEffects_GetWaterCpuMs','GPU':'SIM_GetWaterGpuMs','TILES':'FieldVisualEffects_GetWaterCount'}
            has_metrics=all(name in symbols for name in metrics.values())
            defines=[f'-D{key}_ADDRESS={symbols.get(name,0)}' for key,name in {**required,**metrics}.items()]
            library=temp/(label+'.so')
            subprocess.run(flags+defines+[f'-DHAS_METRICS={int(has_metrics)}','-fPIC','-shared','-I/usr/include/SDL2',str(ROOT/'tools/scripts/field_water_benchmark.c'),'-ldl','-o',str(library)],cwd=build,check=True)
            shutil.copy2(binary,runtime/'main')
            for scale in args.scales:
                for shore in [True,False]:
                    trace=temp/'trace.csv'
                    (runtime/'sim_config.ini').write_text(f'[General]\nInternalResolutionScale={scale}\nWindowWidth=960\nWindowHeight=540\nFullScreen=false\nScreenLayout=top_only\nVsyncInterval=0\nCapFrameRate=false\n')
                    env=dict(os.environ,SDL_VIDEODRIVER='offscreen',SDL_AUDIODRIVER='dummy',LD_PRELOAD=str(library),PLATINUM_FRAME_TRACE=str(trace),PLATINUM_GPU_TRACE='1',WATER_SHORE=str(int(shore)))
                    print(f'Running {label}, {scale}x, {"shore" if shore else "open water"}',flush=True)
                    run=subprocess.run(['./main'],cwd=runtime,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=180)
                    if run.returncode:raise RuntimeError(run.stdout[-4000:])
                    frames={int(row['frame']):row for row in csv.DictReader(trace.open())}
                    gpu=next(line.split(',',1)[1] for line in run.stdout.splitlines() if line.startswith('WATER_GPU,'))
                    for phase in ['off','on']:
                        samples=[line.split(',') for line in run.stdout.splitlines() if line.startswith('WATER_SAMPLE,'+phase+',')]
                        def describe(values):
                            ordered=sorted(values);return {'median_ms':statistics.median(ordered),'p95_ms':ordered[int((len(ordered)-1)*.95)]}
                        rows=[frames[int(sample[2])] for sample in samples if int(sample[2]) in frames]
                        result={'version':label,'scale':scale,'scene':'shore' if shore else 'open','water':phase,'gpu':gpu,'samples':len(rows)}
                        result['work_ns']=describe([int(row['work_ns'])/1e6 for row in rows])
                        sample_frames={int(sample[2]) for sample in samples}
                        valid_gpu=[int(row['gpu_ns'])/1e6 for row in rows if int(row['gpu_ns'])>0 and int(row['gpu_result_frame']) in sample_frames]
                        result['valid_gpu_samples']=len(valid_gpu)
                        result['gpu_ns']=describe(valid_gpu) if valid_gpu else None
                        if has_metrics:
                            result['water_cpu']=describe([float(sample[3]) for sample in samples]);result['water_gpu']=describe([float(sample[4]) for sample in samples]);result['tiles']=statistics.median([int(sample[5]) for sample in samples])
                        results.append(result);args.output.write_text(json.dumps(results,indent=2))
    print(json.dumps(results,indent=2))
if __name__=='__main__':main()
