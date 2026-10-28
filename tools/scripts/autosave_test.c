#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static time_t seconds, wallSeconds = 1793188800;
static int failRename, failSync;

static int TestClock(clockid_t clock, struct timespec *value)
{
    value->tv_sec = seconds;
    value->tv_nsec = 0;
    return 0;
}

static time_t TestTime(time_t *value)
{
    if (value) *value = wallSeconds;
    return wallSeconds;
}

static int TestRename(const char *from, const char *to)
{
    return failRename ? -1 : rename(from, to);
}

static int TestSync(int fd)
{
    return failSync ? -1 : fsync(fd);
}

#define clock_gettime TestClock
#define time TestTime
#define rename TestRename
#define fsync TestSync
#include "../../src/port/autosave.c"
#undef clock_gettime
#undef time
#undef rename
#undef fsync

const char *CARD_PortSavePath = "save2.bin";
static BOOL fadeFinished = TRUE, forcedMovement, communication;
static BOOL safari, palPark, failSave;
static int moveState, saveCalls;

BOOL IsScreenFadeDone(void) { return fadeFinished; }
BOOL CommManager_IsInitializedNotPoketch(void) { return communication; }
int PlayerAvatar_GetPlayerMoveState(const PlayerAvatar *avatar) { return moveState; }
BOOL PlayerAvatar_CheckForcedMovement(PlayerAvatar *avatar) { return forcedMovement; }
VarsFlags *SaveData_GetVarsFlags(SaveData *save) { return (VarsFlags *)save; }
BOOL SystemFlag_CheckSafariGameActive(VarsFlags *flags) { return safari; }
BOOL SystemFlag_CheckInPalPark(VarsFlags *flags) { return palPark; }

BOOL FieldSystem_Save(FieldSystem *field)
{
    saveCalls++;
    field->saveData->globalCounter++;
    field->saveData->blockOffsets[0] = !field->saveData->blockOffsets[0];
    field->saveData->dataExists = TRUE;
    field->saveData->isNewGameData = FALSE;
    field->saveData->fullSaveRequired = FALSE;
    field->location->x++;
    FILE *file = fopen(CARD_PortSavePath, "wb");
    assert(file);
    assert(fprintf(file, "new save %u", field->saveData->body.data[0]) >= 0);
    assert(fclose(file) == 0);
    return !failSave;
}

static void WriteFile(const char *path, const char *bytes)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs(bytes, file) >= 0);
    assert(fclose(file) == 0);
}

static void ExpectFile(const char *path, const char *expected)
{
    char bytes[64] = { 0 };
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fread(bytes, 1, sizeof(bytes) - 1, file) == strlen(expected));
    assert(strcmp(bytes, expected) == 0);
    assert(fclose(file) == 0);
}

static int CountBackups(const char *base)
{
    DIR *dir = opendir("autosave");
    assert(dir);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, base, strlen(base)) == 0) count++;
    }
    closedir(dir);
    return count;
}

static void ExpectHistory(int first, int last)
{
    DIR *dir = opendir("autosave");
    assert(dir);
    int seen[64] = { 0 };
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "save2.bin.", 10) != 0) continue;
        char path[512];
        snprintf(path, sizeof(path), "autosave/%s", entry->d_name);
        FILE *file = fopen(path, "rb");
        assert(file);
        unsigned counter;
        assert(fscanf(file, "new save %u", &counter) == 1);
        assert(counter >= first && counter <= last);
        assert(!seen[counter]++);
        fclose(file);
    }
    closedir(dir);
    for (int i = first; i <= last; i++) assert(seen[i]);
}

int main(void)
{
    static SaveData save = { .isNewGameData = TRUE, .fullSaveRequired = TRUE };
    // Location normally points inside SaveData, and must roll back with it.
    FieldProcessManager manager = { .parent = (ApplicationManager *)1 };
    FieldSystem field = {
        .processManager = &manager, .saveData = &save,
        .location = (Location *)&save.body.data[32],
        .unk_04 = (FieldSystem_sub2 *)1, .mapObjMan = (MapObjectManager *)1,
        .playerAvatar = (PlayerAvatar *)1, .runningFieldMap = TRUE
    };
    WriteFile("save2.bin", "old save");
    WriteFile("save.bin", "legacy save");
    PC_AutosaveReset();
    seconds = 119;
    PC_AutosaveUpdate(&field);
    assert(saveCalls == 0);
    seconds = 120;

    // Dialogue, menus, battles and transitions own a task, child or paused map.
    BOOL *blocked[] = { &manager.pause, &manager.kill, &save.state.locked,
        &forcedMovement, &communication, &safari, &palPark };
    for (unsigned i = 0; i < sizeof(blocked) / sizeof(*blocked); i++) {
        *blocked[i] = TRUE;
        PC_AutosaveUpdate(&field);
        assert(saveCalls == 0);
        *blocked[i] = FALSE;
    }
    manager.child = (ApplicationManager *)1;
    PC_AutosaveUpdate(&field);
    manager.child = NULL;
    manager.parent = NULL;
    PC_AutosaveUpdate(&field);
    manager.parent = (ApplicationManager *)1;
    field.task = (FieldTask *)1;
    PC_AutosaveUpdate(&field);
    field.task = NULL;
    field.runningFieldMap = FALSE;
    PC_AutosaveUpdate(&field);
    field.runningFieldMap = TRUE;
    for (int type = MAP_LOAD_TYPE_UNDERGROUND; type < MAP_LOAD_TYPE_MAX; type++) {
        field.mapLoadType = type;
        PC_AutosaveUpdate(&field);
    }
    field.mapLoadType = MAP_LOAD_TYPE_OVERWORLD;
    fadeFinished = FALSE;
    PC_AutosaveUpdate(&field);
    fadeFinished = TRUE;
    moveState = PLAYER_MOVE_STATE_START;
    PC_AutosaveUpdate(&field);
    moveState = PLAYER_MOVE_STATE_MOVING;
    PC_AutosaveUpdate(&field);
    assert(saveCalls == 0);
    moveState = PLAYER_MOVE_STATE_END;
    seconds = 150;
    save.body.data[0] = 1; // Gameplay progress is independent of save bookkeeping.
    SaveData snapshot = save;
    PC_AutosaveUpdate(&field);
    assert(saveCalls == 1 && memcmp(&save, &snapshot, sizeof(save)) == 0);
    assert(strcmp(CARD_PortSavePath, "save2.bin") == 0);
    ExpectFile("save2.bin", "old save");
    assert(CountBackups("save2.bin.") == 1);

    char stamp[32], firstBackup[256];
    struct tm local;
    assert(localtime_r(&wallSeconds, &local));
    assert(strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &local));
    snprintf(firstBackup, sizeof(firstBackup), "autosave/save2.bin.%s-0000.bak", stamp);
    ExpectFile(firstBackup, "new save 1");
    WriteFile("autosave/save1.bin.unrelated.bak", "unrelated slot");
    WriteFile("autosave/notes.bak", "unrelated file");

    seconds = 269;
    PC_AutosaveUpdate(&field);
    assert(saveCalls == 1);
    seconds = 270;
    save.body.data[0] = 2;
    PC_AutosaveUpdate(&field);
    assert(saveCalls == 2);
    ExpectFile(firstBackup, "new save 1");

    // Repeated timestamps, including a timer/process reset, cannot overwrite
    // archives or cause a new archive to be evicted because a suffix was reused.
    for (int i = 3; i <= 25; i++) {
        PC_AutosaveReset();
        seconds += 120;
        save.body.data[0] = i;
        snapshot = save;
        PC_AutosaveUpdate(&field);
        assert(saveCalls == i && memcmp(&save, &snapshot, sizeof(save)) == 0);
        assert(strcmp(CARD_PortSavePath, "save2.bin") == 0);
        ExpectFile("save2.bin", "old save");
        assert(CountBackups("save2.bin.") == (i < 10 ? i : 10));
    }
    assert(access(firstBackup, F_OK) != 0);
    ExpectHistory(16, 25);
    ExpectFile("autosave/save1.bin.unrelated.bak", "unrelated slot");
    ExpectFile("autosave/notes.bak", "unrelated file");

    snapshot = save;
    failSave = TRUE;
    seconds += 120;
    PC_AutosaveUpdate(&field);
    assert(saveCalls == 26 && memcmp(&save, &snapshot, sizeof(save)) == 0);
    ExpectFile("save2.bin", "old save");
    PC_AutosaveUpdate(&field);
    assert(saveCalls == 26); // Failed writes wait another two minutes to retry.
    failSave = FALSE;
    failSync = TRUE;
    seconds += 120;
    PC_AutosaveUpdate(&field);
    assert(saveCalls == 27 && memcmp(&save, &snapshot, sizeof(save)) == 0);
    failSync = FALSE;
    failRename = TRUE;
    seconds += 120;
    PC_AutosaveUpdate(&field);
    assert(saveCalls == 28 && memcmp(&save, &snapshot, sizeof(save)) == 0);
    failRename = FALSE;
    ExpectFile("save2.bin", "old save");
    assert(CountBackups("save2.bin.") == 10);
    ExpectHistory(16, 25); // Failure must not evict any successful history.
    assert(strcmp(CARD_PortSavePath, "save2.bin") == 0);

    // Only an explicit manual save updates the normal slot and live bookkeeping.
    assert(FieldSystem_Save(&field));
    assert(saveCalls == 29 && save.globalCounter == 1 && save.dataExists);
    assert(!save.isNewGameData && !save.fullSaveRequired);
    ExpectFile("save2.bin", "new save 25");
    assert(CountBackups("save2.bin.") == 10);
    ExpectHistory(16, 25);

    // A second slot rotates independently, including its first save on disk.
    CARD_PortSavePath = "save3.bin";
    save.isNewGameData = TRUE;
    save.dataExists = FALSE;
    save.fullSaveRequired = TRUE;
    save.body.data[0] = 26;
    snapshot = save;
    PC_AutosaveReset();
    seconds += 119;
    PC_AutosaveUpdate(&field);
    assert(saveCalls == 29);
    seconds++;
    PC_AutosaveUpdate(&field);
    assert(saveCalls == 30);
    assert(access("save3.bin", F_OK) != 0);
    assert(memcmp(&save, &snapshot, sizeof(save)) == 0);
    assert(strcmp(CARD_PortSavePath, "save3.bin") == 0);
    assert(CountBackups("save3.bin.") == 1);
    char slot3Backup[256];
    snprintf(slot3Backup, sizeof(slot3Backup), "autosave/save3.bin.%s-0000.bak", stamp);
    ExpectFile(slot3Backup, "new save 26");
    for (int i = 0; i < 12; i++) {
        seconds += 120;
        PC_AutosaveUpdate(&field);
    }
    assert(CountBackups("save3.bin.") == 10);
    assert(access("save3.bin", F_OK) != 0);
    assert(memcmp(&save, &snapshot, sizeof(save)) == 0);
    ExpectHistory(16, 25);
    ExpectFile("save2.bin", "new save 25");

    // A non-directory autosave path fails without changing the slot or RAM.
    assert(mkdir("nested", 0700) == 0);
    WriteFile("nested/autosave", "blocked directory");
    WriteFile("nested/save1.bin", "nested old save");
    CARD_PortSavePath = "nested/save1.bin";
    snapshot = save;
    seconds += 120;
    PC_AutosaveUpdate(&field);
    ExpectFile("nested/save1.bin", "nested old save");
    assert(memcmp(&save, &snapshot, sizeof(save)) == 0);
    assert(remove("nested/autosave") == 0);
    seconds += 120;
    PC_AutosaveUpdate(&field);
    char nestedBackup[256];
    snprintf(nestedBackup, sizeof(nestedBackup), "nested/autosave/save1.bin.%s-0000.bak", stamp);
    ExpectFile(nestedBackup, "new save 26");
    ExpectFile("nested/save1.bin", "nested old save");
    assert(memcmp(&save, &snapshot, sizeof(save)) == 0);
    assert(strcmp(CARD_PortSavePath, "nested/save1.bin") == 0);
    ExpectFile("save.bin", "legacy save");

    DIR *dir = opendir(".");
    struct dirent *entry;
    while ((entry = readdir(dir))) assert(strstr(entry->d_name, ".autosave-") == NULL);
    closedir(dir);
    dir = opendir("nested");
    while ((entry = readdir(dir))) assert(strstr(entry->d_name, ".autosave-") == NULL);
    closedir(dir);
    dir = opendir("autosave");
    while ((entry = readdir(dir))) assert(strstr(entry->d_name, ".autosave-") == NULL);
    closedir(dir);
    dir = opendir("nested/autosave");
    while ((entry = readdir(dir))) assert(strstr(entry->d_name, ".autosave-") == NULL);
    closedir(dir);
    puts("PASS: separate autosaves, manual save preservation, timing, field safety, retention, collisions and failure recovery");
    return 0;
}
