// Exercise the native repair pass and real DS compositor, including 4K GPU timings.
#include <SDL2/SDL.h>
#include <simulator/glad/glad.h>
#include <simulator/sim_field_coverage.hpp>
#include <array>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static GLuint Shader(GLenum type, const std::string &source)
{
    GLuint shader = glCreateShader(type);
    const char *text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << log;
        std::abort();
    }
    return shader;
}

static std::string Source(const std::string &path)
{
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    std::string raw = text.str();
    return raw.substr(raw.find('(') + 1, raw.rfind(')') - raw.find('(') - 1);
}

int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    const bool benchmark = argc == 3;
    const std::string directory = argv[1];
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *window = SDL_CreateWindow("Terrain coverage", 0, 0, benchmark ? 3840 : 356, benchmark ? 2160 : 192, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    assert(window);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    assert(context && gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress));
    std::cout << "GPU: " << glGetString(GL_RENDERER) << '\n';
    SIMFieldCoverage coverage;
    coverage.Init(Source(directory + "/bloom_vertex.glsl").c_str(),
        Source(directory + "/field_coverage_fragment.glsl").c_str(), 448, 384);
    assert(coverage.Ready());
    GLuint program = glCreateProgram();
    glAttachShader(program, Shader(GL_VERTEX_SHADER, R"(
#version 420 core
smooth out vec2 fragTexCoord;
void main() {
    vec2 p = gl_VertexID == 0 ? vec2(-1,-1) : (gl_VertexID == 1 ? vec2(3,-1) : vec2(-1,3));
    gl_Position = vec4(p,0,1);
    fragTexCoord = vec2(p.x * .5 + .5, p.y * .25 + .75);
})"));
    glAttachShader(program, Shader(GL_FRAGMENT_SHADER, Source(directory + "/g2_fragment.glsl")));
    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[8192];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << log;
        std::abort();
    }
    glUseProgram(program);
    auto integer = [&](const char *name, int value) { glUniform1i(glGetUniformLocation(program, name), value); };
    auto scalar = [&](const char *name, float value) { glUniform1f(glGetUniformLocation(program, name), value); };
    auto texture = [&](int unit, GLenum target, GLint format, int w, int h, GLenum dataFormat, GLenum type, const void *data) {
        GLuint id;
        glGenTextures(1, &id);
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(target, id);
        if (target == GL_TEXTURE_2D_ARRAY)
            glTexImage3D(target, 0, format, w, h, 4, 0, dataFormat, type, data);
        else
            glTexImage2D(target, 0, format, w, h, 0, dataFormat, type, data);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return id;
    };
    using Pixel = std::array<unsigned char, 4>;
    const Pixel green = { 64, 160, 80, 255 }, empty = {};
    std::vector<Pixel> scene(448 * 384, green);
    GLuint sceneTexture = texture(0, GL_TEXTURE_2D, GL_RGBA8, 448, 384, GL_RGBA, GL_UNSIGNED_BYTE, scene.data());
    std::vector<unsigned char> masks(448 * 384 * 2, 0x3f);
    texture(1, GL_TEXTURE_2D, GL_RG8UI, 448, 384, GL_RG_INTEGER, GL_UNSIGNED_BYTE, masks.data());
    std::vector<unsigned short> blend(384 * 4, 0);
    texture(2, GL_TEXTURE_2D, GL_RGBA16UI, 1, 384, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, blend.data());
    texture(3, GL_TEXTURE_2D_ARRAY, GL_RGBA8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    texture(4, GL_TEXTURE_2D, GL_RGBA8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, empty.data());
    // A uniform blue fog: the repaired sample must bring its fog along.
    const Pixel fog = { 0, 0, 128, 128 };
    std::vector<Pixel> fogPixels(448 * 384, fog);
    GLuint fogTexture = texture(5, GL_TEXTURE_2D, GL_RGBA8, 448, 384, GL_RGBA, GL_UNSIGNED_BYTE, fogPixels.data());
    integer("windowMaskTexture", 1);
    integer("scanlineBlendTexture", 2);
    integer("fieldParticlesTexture", 3);
    for (int i = 0; i < 4; i++) integer(("bgTexture[" + std::to_string(i) + "]").c_str(), i == 0 ? 0 : 4);
    for (int i = 0; i < 5; i++) integer(("objTexture[" + std::to_string(i) + "]").c_str(), 4);
    for (const char *name : { "myTexture", "bloomTexture", "objWindowTexture", "sideAverageTexture" }) integer(name, 4);
    integer("g3FogTexture", 5);
    integer("topScreen", 1);
    integer("bg0as3d", 1);
    integer("fieldTransitionMask", -1);
    integer("edgeLayer", -1);
    scalar("presentationWidth", 356);
    scalar("fieldWidth", 356);
    glUniform4f(glGetUniformLocation(program, "backdropColor"), 0, 0, 0, 1);
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glViewport(0, 0, 356, 192);
    const int x = 184, y = 318; // Save1's seam in the 448x384 scene texture.
    auto sample = [&](int dx = 0, int dy = 0) {
        Pixel result;
        glReadPixels(x - 46 + dx, y - 192 + dy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
        return result;
    };
    auto draw = [&](bool repair) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 448, 384, GL_RGBA, GL_UNSIGNED_BYTE, scene.data());
        if (repair) coverage.Run(sceneTexture, fogTexture);
        glBindTexture(GL_TEXTURE_2D, repair ? coverage.Color() : sceneTexture);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, repair ? coverage.Fog() : fogTexture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        assert(glGetError() == GL_NO_ERROR);
        return sample();
    };
    const Pixel intact = draw(false);
    scene[y * 448 + x] = empty;
    const Pixel black = { 0, 0, 0, 255 };
    assert(draw(false) == black);
    assert(draw(true) == intact); // Opaque coverage and matching fog restored.
    assert(sample(-1) == intact && sample(1) == intact);
    scene[y * 448 + x + 1] = empty;
    assert(draw(true) == black); // Larger openings are preserved.
    scene[y * 448 + x + 1] = green;
    scene[(y + 1) * 448 + x + 1] = empty;
    assert(draw(true) == black); // Even diagonal transparency rejects repair.
    scene[(y + 1) * 448 + x + 1] = green;
    scene[y * 448 + x] = { 32, 80, 40, 128 };
    const Pixel translucent = draw(false);
    assert(draw(true) == translucent); // No changes to translucent samples.
    scene[y * 448 + x] = green;
    scene[y * 448 + x - 1] = { 0, 0, 0, 255 };
    assert(draw(true) == intact); // Existing opaque texture pixels stay exact.
    scene[y * 448 + x] = empty;
    assert(draw(true) == intact); // Continue the matching pair, not the dark neighbor.
    integer("bg0as3d", 0);
    const Pixel background = draw(false);
    assert(draw(true) == background); // Ordinary 2D backgrounds are unaffected.
    // The pass must preserve the renderer's framebuffer, viewport, masks,
    // texture bindings and raster state, even when called after 3D rendering.
    glViewport(3, 4, 7, 8);
    for (GLenum capability : { GL_DEPTH_TEST, GL_CULL_FACE, GL_SCISSOR_TEST, GL_STENCIL_TEST }) glEnable(capability);
    glEnablei(GL_BLEND, 0);
    glDisablei(GL_BLEND, 1);
    glColorMaski(0, GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    glActiveTexture(GL_TEXTURE6);
    coverage.Run(sceneTexture, fogTexture);
    GLint viewport[4], framebuffer, current, active, boundVao;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &current);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVao);
    GLboolean mask[4]; glGetBooleani_v(GL_COLOR_WRITEMASK, 0, mask);
    assert(viewport[0] == 3 && viewport[3] == 8 && framebuffer == 0);
    assert(current == (GLint)program && active == GL_TEXTURE6 && boundVao == (GLint)vao);
    assert(!mask[0] && mask[1] && !mask[2] && mask[3]);
    assert(glIsEnabledi(GL_BLEND, 0) && !glIsEnabledi(GL_BLEND, 1));
    for (GLenum capability : { GL_DEPTH_TEST, GL_CULL_FACE, GL_SCISSOR_TEST, GL_STENCIL_TEST }) {
        assert(glIsEnabled(capability));
        glDisable(capability);
    }
    glDisablei(GL_BLEND, 0);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    assert(glGetError() == GL_NO_ERROR);
    if (benchmark) {
        integer("bg0as3d", 1);
        glViewport(0, 0, 3840, 2160);
        for (bool repair : { false, true }) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, repair ? coverage.Color() : sceneTexture);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, repair ? coverage.Fog() : fogTexture);
            if (repair) coverage.Run(sceneTexture, fogTexture);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glFinish();
            GLuint query;
            glGenQueries(1, &query);
            glBeginQuery(GL_TIME_ELAPSED, query);
            for (int i = 0; i < 30; i++) {
                if (repair) coverage.Run(sceneTexture, fogTexture);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            glEndQuery(GL_TIME_ELAPSED);
            glFinish();
            GLuint64 elapsed;
            glGetQueryObjectui64v(query, GL_QUERY_RESULT, &elapsed);
            glDeleteQueries(1, &query);
            std::cout << "4K compositor, native repair " << (repair ? "on: " : "off: ")
                << elapsed / 30e6 << " ms/frame\n";
        }
    }
    coverage.Destroy();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "PASS: isolated terrain coverage, fog, texture continuity, transparent openings, alpha, 2D backgrounds and GL state restoration\n";
}
