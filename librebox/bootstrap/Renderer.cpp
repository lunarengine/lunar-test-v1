// Renderer.cpp
#include "Renderer.h"
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <cfloat>
#include <unordered_map>
#include <cstdint>
#include "bootstrap/Game.h"
#include "bootstrap/instances/InstanceTypes.h"
#include "bootstrap/instances/BasePart.h"      // for CF
#include "core/datatypes/CFrame.h"             // for CF

extern std::shared_ptr<Game> g_game;

// ---------------- Tunables ----------------
static float kMaxDrawDistance = 10000.0f;
static float kFovPaddingDeg   = 20.0f;

// shadow parameter definitions
static float kShadowMaxDistance = 200.0f;  // how far from the camera to cover with shadows
static int   kShadowRes         = 1536;    // per-cascade resolution
static float kPCFStep           = 1.0f;    // pcf step in texels
static bool  kCullBackFace      = true;
static bool  kStabilizeShadow   = true;    // snap to texel grid to prevent swimming

// CSM controls
static int   kNumCascades       = 2;
static float kCameraNear        = 0.4f;     // must match scene near

// ---- controls (ambient zero, parity by default) ----
static bool     kUseClockTime = true;               // keep original sun by default
static float    kClockTime    = 12.0f;              // 0..24 hours when enabled
static float    kBrightness   = 2.0f;               // original sunStrength default
static float    kExposure     = 0.85f;              // new exposure knob, <1 darker, >1 brighter
static ::Color3 kAmbient      = {0.0f, 0.0f, 0.0f}; // Ambient = 0

static inline float LenSq(Vector3 v){ return v.x*v.x + v.y*v.y + v.z*v.z; }
struct SavedWin { int x,y,w,h; bool valid=false; } g_saved;

inline ::Color ToRaylibColor(const ::Color3& c, float alpha = 1.0f) {
    return {
        (unsigned char)std::lroundf(std::clamp(c.r, 0.0f, 1.0f) * 255.0f),
        (unsigned char)std::lroundf(std::clamp(c.g, 0.0f, 1.0f) * 255.0f),
        (unsigned char)std::lroundf(std::clamp(c.b, 0.0f, 1.0f) * 255.0f),
        (unsigned char)std::lroundf(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)
    };
}

static void EnterBorderlessFullscreen(int monitor = -1) {
    if (!g_saved.valid) {
        Vector2 p = GetWindowPosition();
        g_saved = { (int)p.x, (int)p.y, GetScreenWidth(), GetScreenHeight(), true };
    }
    if (monitor < 0) monitor = GetCurrentMonitor();
    Vector2 mpos = GetMonitorPosition(monitor);
    int mw = GetMonitorWidth(monitor), mh = GetMonitorHeight(monitor);
    SetWindowState(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TOPMOST);
    SetWindowSize(mw, mh);
    SetWindowPosition((int)mpos.x, (int)mpos.y);
}
static void ExitBorderlessFullscreen() {
    ClearWindowState(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TOPMOST);
    if (g_saved.valid) { SetWindowSize(g_saved.w, g_saved.w); SetWindowPosition(g_saved.x, g_saved.y); }
    else RestoreWindow();
}

// ---- Helpers ----
// Exact parity at ClockTime = 12.0  ->  normalize(1, -1, 1)
static Vector3 SunDirFromClock(float clockHours){
    float t = fmodf(fmaxf(clockHours, 0.0f), 24.0f);
    const float phi = PI*0.25f; // fixed azimuth 45°
    const float alphaNoon = asinf(1.0f / sqrtf(3.0f)); // ≈35.264°
    float w = (2.0f*PI/24.0f) * (t - 6.0f);            // 6->sunrise, 12->noon, 18->sunset
    float alpha = alphaNoon * sinf(w);                 // elevation
    float ca = cosf(alpha), sa = sinf(alpha);
    float cphi = cosf(phi),  sphi = sinf(phi);
    return { ca*cphi, -sa, ca*sphi };                  // from sun to scene
}

// ---------------- Shaders ----------------
#define GLSL_VERSION 330

// Lit + CSM shadows (PCF), hemispheric, spec, fresnel, AO
static const char* LIT_VS = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 lightVP0;
uniform mat4 lightVP1;
uniform mat4 lightVP2;

uniform float normalBiasWS0;
uniform float normalBiasWS1;
uniform float normalBiasWS2;

out vec3 vN;
out vec3 vWPos;
out vec4 vLS0;
out vec4 vLS1;
out vec4 vLS2;

void main(){
    mat3 nmat = mat3(transpose(inverse(matModel)));
    vN = normalize(nmat * vertexNormal);
    vec4 worldPos = matModel * vec4(vertexPosition,1.0);
    vWPos = worldPos.xyz;

    // apply normal-space small offset per-cascade to avoid contact gaps
    vec3 Nw = normalize(vN);
    vec4 worldPos0 = worldPos + vec4(Nw * normalBiasWS0, 0.0);
    vec4 worldPos1 = worldPos + vec4(Nw * normalBiasWS1, 0.0);
    vec4 worldPos2 = worldPos + vec4(Nw * normalBiasWS2, 0.0);

    vLS0 = lightVP0 * worldPos0;
    vLS1 = lightVP1 * worldPos1;
    vLS2 = lightVP2 * worldPos2;

    gl_Position = mvp * vec4(vertexPosition,1.0);
})";

static const char* LIT_VS_INST = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;

// Per-instance model matrix provided as vertex attribute
in mat4 instanceTransform;

uniform mat4 mvp;
uniform mat4 lightVP0;
uniform mat4 lightVP1;
uniform mat4 lightVP2;

uniform float normalBiasWS0;
uniform float normalBiasWS1;
uniform float normalBiasWS2;

out vec3 vN;
out vec3 vWPos;
out vec4 vLS0;
out vec4 vLS1;
out vec4 vLS2;

void main(){
    mat3 nmat = mat3(transpose(inverse(instanceTransform)));
    vN = normalize(nmat * vertexNormal);

    vec4 worldPos = instanceTransform * vec4(vertexPosition,1.0);
    vWPos = worldPos.xyz;

    vec3 Nw = normalize(vN);
    vec4 worldPos0 = worldPos + vec4(Nw * normalBiasWS0, 0.0);
    vec4 worldPos1 = worldPos + vec4(Nw * normalBiasWS1, 0.0);
    vec4 worldPos2 = worldPos + vec4(Nw * normalBiasWS2, 0.0);

    vLS0 = lightVP0 * worldPos0;
    vLS1 = lightVP1 * worldPos1;
    vLS2 = lightVP2 * worldPos2;

    gl_Position = mvp * worldPos;
})";

static const char* LIT_FS = R"(#version 330
in vec3 vN;
in vec3 vWPos;
in vec4 vLS0;
in vec4 vLS1;
in vec4 vLS2;

out vec4 FragColor;

uniform vec3 viewPos;

// Lighting
uniform vec3 sunDir;
uniform vec3 skyColor;
uniform vec3 groundColor;
uniform float hemiStrength;
uniform float sunStrength;
uniform vec3 ambientColor;    // flat ambient (default 0)

// Spec/Fresnel
uniform float specStrength;   // 0..1 intensity
uniform float shininess;      // 8..128
uniform float fresnelStrength;// 0..1

// AO
uniform float aoStrength;     // 0..1
uniform float groundY;        // ground plane Y

// CSM - Use regular sampler2D for compatibility with Raylib's render textures
uniform sampler2D shadowMap0;
uniform sampler2D shadowMap1;
uniform sampler2D shadowMap2;
uniform int   shadowMapResolution;
uniform float pcfStep;        // in texels
uniform float biasMin;        // min bias
uniform float biasMax;        // max bias
uniform vec3  cascadeSplits;  // [d0,d1,f]

// transition band (fraction of split distance)
uniform float transitionFrac; // e.g. 0.10..0.20 of split distance

// exposure
uniform float exposure;       // new exposure control

// raylib default material color (tint * material diffuse)
uniform vec4 colDiffuse;

float SampleShadow(sampler2D smap, vec3 proj, float ndl){
    float bias = mix(biasMax, biasMin, ndl);
    float step = pcfStep / float(shadowMapResolution);
    float sum = 0.0;
    for(int x=-1; x<=1; ++x)
    for(int y=-1; y<=1; ++y){
        vec2 off = vec2(x,y) * step;
        // Manual depth comparison for regular sampler2D
        float shadowDepth = texture(smap, proj.xy + off).r;
        float currentDepth = proj.z - bias;
        sum += (shadowDepth >= currentDepth) ? 1.0 : 0.0;
    }
    return sum / 9.0;
}

float ShadowForCascade(int idx, vec3 proj, float ndl){
    if (proj.x<0.0 || proj.x>1.0 || proj.y<0.0 || proj.y>1.0 || proj.z<0.0 || proj.z>1.0)
        return 1.0;
    if (idx==0) return SampleShadow(shadowMap0, proj, ndl);
    if (idx==1) return SampleShadow(shadowMap1, proj, ndl);
    return SampleShadow(shadowMap2, proj, ndl);
}

// blend across cascade boundaries using a symmetric transition band
float ShadowBlend(vec3 proj0, vec3 proj1, vec3 proj2, float ndl, float viewDepth, vec3 splits){
    // compute absolute band sizes in world units based on splits
    float d0 = splits.x;
    float d1 = splits.y;
    // clamp a reasonable fraction
    float frac = clamp(transitionFrac, 0.0, 0.5);
    float band0 = max(0.001, d0 * frac);
    float band1 = max(0.001, d1 * frac);

    // below first band -> full cascade0
    if (viewDepth <= d0 - band0) {
        return ShadowForCascade(0, proj0, ndl);
    }
    // blend between 0 and 1
    if (viewDepth < d0 + band0) {
        float t = smoothstep(d0 - band0, d0 + band0, viewDepth);
        float s0 = ShadowForCascade(0, proj0, ndl);
        float s1 = ShadowForCascade(1, proj1, ndl);
        return mix(s0, s1, t);
    }
    // middle region -> full cascade1
    if (viewDepth <= d1 - band1) {
        return ShadowForCascade(1, proj1, ndl);
    }
    // blend between 1 and 2
    if (viewDepth < d1 + band1) {
        float t = smoothstep(d1 - band1, d1 + band1, viewDepth);
        float s1 = ShadowForCascade(1, proj1, ndl);
        float s2 = ShadowForCascade(2, proj2, ndl);
        return mix(s1, s2, t);
    }
    // far -> cascade2
    return ShadowForCascade(2, proj2, ndl);
}

void main(){
    vec3 N = normalize(vN);
    vec3 L = normalize(-sunDir);
    vec3 V = normalize(viewPos - vWPos);
    vec3 H = normalize(L + V);

    // Hemispheric ambient
    float t = clamp(N.y*0.5 + 0.5, 0.0, 1.0);
    vec3 hemi = mix(groundColor, skyColor, t) * hemiStrength;

    // Diffuse term
    float ndl = max(dot(N,L), 0.0);

    // Specular
    float spec = pow(max(dot(N,H), 0.0), shininess) * specStrength * 0;

    // Fresnel (Schlick)
    float F0 = 0.02;
    float FH = pow(1.0 - max(dot(N,V),0.0), 5.0);
    float fresnel = mix(F0, 1.0, FH) * fresnelStrength;

    // AO
    float aoHem = mix(0.4, 1.0, t);
    float height = max(vWPos.y - groundY, 0.0);
    float aoGround = clamp(1.0 - exp(-height*0.25), 0.6, 1.0);
    float ao = mix(1.0, aoHem*aoGround, aoStrength);

    // view-space depth as distance camera -> worldPos (monotonic)
    float viewDepth = length(viewPos - vWPos);

    // compute all three projections once
    vec3 p0 = (vLS0.xyz / vLS0.w) * 0.5 + 0.5;
    vec3 p1 = (vLS1.xyz / vLS1.w) * 0.5 + 0.5;
    vec3 p2 = (vLS2.xyz / vLS2.w) * 0.5 + 0.5;

    // blended shadow across cascade boundaries
    float shadow = ShadowBlend(p0, p1, p2, ndl, viewDepth, cascadeSplits);

    // Convert material color from sRGB to linear before lighting
    vec3 base = pow(colDiffuse.rgb, vec3(2.2));

    // Diffuse sunlight contribution
    float sunTerm = sunStrength * ndl * shadow;

    // Block specular and fresnel inside shadow
    spec    *= shadow;
    fresnel *= shadow;

    // Final color
    vec3 color = base * (hemi*ao + sunTerm) + spec + fresnel;
    color += base * ambientColor; // flat ambient

    // apply exposure before gamma
    color *= exposure;

    // Gamma correct (linear -> sRGB)
    color = pow(max(color, vec3(0.0)), vec3(1.0/2.2));
    FragColor = vec4(color, colDiffuse.a);
})";

// ---------------- Sky shader ----------------
static const char* SKY_VS = R"(#version 330
in vec3 vertexPosition;
out vec3 dir;
uniform mat4 matView;
uniform mat4 matProjection;
void main() {
    dir = vertexPosition;
    mat3 R = mat3(matView);
    mat4 viewNoTrans = mat4(
        vec4(R[0], 0.0),
        vec4(R[1], 0.0),
        vec4(R[2], 0.0),
        vec4(0.0, 0.0, 0.0, 1.0)
    );
    gl_Position = matProjection * viewNoTrans * vec4(vertexPosition, 1.0);
})";

static const char* SKY_FS = R"(#version 330
in vec3 dir;
out vec4 FragColor;
uniform vec3 innerColor; // bottom color (ground)
uniform vec3 outerColor; // top color (sky)
void main() {
    vec3 nd = normalize(dir);
    float t = clamp(nd.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 col = mix(innerColor, outerColor, t);
    // Gamma out so sky matches lit pass
    FragColor = vec4(pow(col, vec3(1.0/2.2)), 1.0);
})";

// ---------------- Shader objects / models / shadow RT ----------------
static Shader gLitShader   = {0};
static Shader gLitShaderInst = {0};
static Shader gSkyShader   = {0};
static Model  gPartModel   = {0}; // cube model used for parts
static Model  gSkyModel    = {0};
static Material gPartMatInst = {0};
static RenderTexture2D gShadowMapCSM[3] = { {0},{0},{0} };

// Sky uniforms
static int u_inner=-1, u_outer=-1, u_transition=-1;

// Lit shader uniform locations
static int u_sunDir=-1, u_sky=-1, u_ground=-1, u_hemi=-1, u_sun=-1;
static int u_spec=-1, u_shiny=-1, u_fresnel=-1, u_viewPos=-1;
static int u_aoStrength=-1, u_groundY=-1, u_ambient=-1;

// CSM uniforms
static int u_lightVP0=-1, u_lightVP1=-1, u_lightVP2=-1;
static int u_shadowMap0=-1, u_shadowMap1=-1, u_shadowMap2=-1;
static int u_shadowRes=-1, u_pcfStep=-1, u_biasMin=-1, u_biasMax=-1;
static int u_splits=-1;
static int u_exposure = -1;
static int u_normalBias0=-1, u_normalBias1=-1, u_normalBias2=-1;

// Instanced shader uniform locations (mirror)
static int ui_sunDir=-1, ui_sky=-1, ui_ground=-1, ui_hemi=-1, ui_sun=-1;
static int ui_spec=-1, ui_shiny=-1, ui_fresnel=-1, ui_viewPos=-1;
static int ui_aoStrength=-1, ui_groundY=-1, ui_ambient=-1;
static int ui_lightVP0=-1, ui_lightVP1=-1, ui_lightVP2=-1;
static int ui_shadowMap0=-1, ui_shadowMap1=-1, ui_shadowMap2=-1;
static int ui_shadowRes=-1, ui_pcfStep=-1, ui_biasMin=-1, ui_biasMax=-1;
static int ui_splits=-1;
static int ui_exposure=-1;
static int ui_normalBias0=-1, ui_normalBias1=-1, ui_normalBias2=-1;
static int ui_transition = -1;

// ---------------- Shadowmap helpers ----------------
static RenderTexture2D LoadShadowmapRenderTexture(int width, int height){
    // Use Raylib's high-level API instead of manual OpenGL calls
    RenderTexture2D target = LoadRenderTexture(width, height);
    
    // Raylib automatically handles framebuffer, color and depth textures
    // This is more compatible across platforms (OpenGL, Metal, DirectX)
    
    return target;
}

static void UnloadShadowmapRenderTexture(RenderTexture2D target){
    // Use Raylib's high-level API for cleanup
    UnloadRenderTexture(target);
}

// --------- CFrame -> axis-angle (for DrawModelEx) ----------
static inline float clampf(float x, float a, float b){ return x < a ? a : (x > b ? b : x); }

static void CFrameToAxisAngle(const CFrame& cf, ::Vector3& axisOut, float& angleDegOut){
    // Row-major 3x3
    const float m00 = cf.R[0], m01 = cf.R[1], m02 = cf.R[2];
    const float m10 = cf.R[3], m11 = cf.R[4], m12 = cf.R[5];
    const float m20 = cf.R[6], m21 = cf.R[7], m22 = cf.R[8];

    const float trace = m00 + m11 + m22;
    float cosA = (trace - 1.0f)*0.5f;
    cosA = clampf(cosA, -1.0f, 1.0f);
    float angle = acosf(cosA);

    if (angle < 1e-6f){
        axisOut = {0.0f, 1.0f, 0.0f};
        angleDegOut = 0.0f;
        return;
    }

    if (fabsf(PI - angle) < 1e-4f){
        // Near 180°, robust axis extraction
        float xx = sqrtf(fmaxf(0.0f, (m00 + 1.0f)*0.5f));
        float yy = sqrtf(fmaxf(0.0f, (m11 + 1.0f)*0.5f));
        float zz = sqrtf(fmaxf(0.0f, (m22 + 1.0f)*0.5f));
        // pick signs from off-diagonals
        xx = copysignf(xx, m21 - m12);
        yy = copysignf(yy, m02 - m20);
        zz = copysignf(zz, m10 - m01);
        float len = sqrtf(xx*xx + yy*yy + zz*zz);
        if (len < 1e-6f) { axisOut = {0.0f,1.0f,0.0f}; angleDegOut = 180.0f; return; }
        axisOut = { xx/len, yy/len, zz/len };
        angleDegOut = 180.0f;
        return;
    }

    float s = 2.0f*sinf(angle);
    ::Vector3 axis = {
        (m21 - m12) / s,
        (m02 - m20) / s,
        (m10 - m01) / s
    };
    float len = sqrtf(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
    if (len < 1e-6f) { axisOut = {0.0f,1.0f,0.0f}; angleDegOut = 0.0f; return; }
    axisOut = { axis.x/len, axis.y/len, axis.z/len };
    angleDegOut = angle * (180.0f/PI);
}

// ---------------- Dynamic shadow helpers ----------------

// Return a safe 'up' vector for the given direction
static inline Vector3 SafeUpForDir(Vector3 d){
    Vector3 up = {0,1,0};
    if (fabsf(Vector3DotProduct(up, d)) > 0.99f) up = {0,0,1};
    return up;
}

// Compute world-space frustum corners for camera slice [nearD, farD]
static void GetFrustumCornersWS(const Camera3D& cam, float nearD, float farD, Vector3 outCorners[8]){
    const float vFov = cam.fovy * (PI/180.0f);
    const float aspect = (float)GetScreenWidth()/(float)GetScreenHeight();

    Vector3 F = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 R = Vector3Normalize(Vector3CrossProduct(F, cam.up));
    Vector3 U = Vector3CrossProduct(R, F);

    float hN = tanf(vFov*0.5f)*nearD, wN = hN*aspect;
    float hF = tanf(vFov*0.5f)*farD,  wF = hF*aspect;

    Vector3 cN = Vector3Add(cam.position, Vector3Scale(F, nearD));
    Vector3 cF = Vector3Add(cam.position, Vector3Scale(F, farD));

    // near
    outCorners[0] = Vector3Add(Vector3Add(cN, Vector3Scale(U,  hN)), Vector3Scale(R,  wN)); // ntr
    outCorners[1] = Vector3Add(Vector3Add(cN, Vector3Scale(U,  hN)), Vector3Scale(R, -wN)); // ntl
    outCorners[2] = Vector3Add(Vector3Add(cN, Vector3Scale(U, -hN)), Vector3Scale(R,  wN)); // nbr
    outCorners[3] = Vector3Add(Vector3Add(cN, Vector3Scale(U, -hN)), Vector3Scale(R, -wN)); // nbl
    // far
    outCorners[4] = Vector3Add(Vector3Add(cF, Vector3Scale(U,  hF)), Vector3Scale(R,  wF)); // ftr
    outCorners[5] = Vector3Add(Vector3Add(cF, Vector3Scale(U,  hF)), Vector3Scale(R, -wF)); // ftl
    outCorners[6] = Vector3Add(Vector3Add(cF, Vector3Scale(U, -hF)), Vector3Scale(R,  wF)); // fbr
    outCorners[7] = Vector3Add(Vector3Add(cF, Vector3Scale(U, -hF)), Vector3Scale(R, -wF)); // fbl
}

// Transform point by matrix (4x4 * vec4(p,1))
static inline Vector3 XformPoint(const Matrix& m, Vector3 p){
    float x = m.m0*p.x + m.m4*p.y + m.m8*p.z + m.m12;
    float y = m.m1*p.x + m.m5*p.y + m.m9*p.z + m.m13;
    float z = m.m2*p.x + m.m6*p.y + m.m10*p.z + m.m14;
    return { x, y, z };
}

// Build a directional light camera tightly fitting the camera frustum slice
// NOTE: outTexelWS returns the world-space size of one shadow map texel for this cascade.
static void BuildLightCameraForSlice(const Camera3D& cam, Vector3 sunDir,
                                     float sliceNear, float sliceFar,
                                     int shadowRes,
                                     int rtW, int rtH,
                                     Camera3D& outCam, Matrix& outLightVP,
                                     float& outTexelWS)
{
    Vector3 cornersWS[8];
    GetFrustumCornersWS(cam, sliceNear, sliceFar, cornersWS);

    // compute centroid
    Vector3 center = {0,0,0};
    for (int i=0;i<8;i++) center = Vector3Add(center, cornersWS[i]);
    center = Vector3Scale(center, 1.0f/8.0f);

    Vector3 upL = SafeUpForDir(Vector3Scale(sunDir, -1.0f));
    // initial view from sun direction
    Matrix lightView = MatrixLookAt(Vector3Add(center, Vector3Scale(Vector3Scale(sunDir, -1.0f), 200.0f)), center, upL);

    // compute bounds in light space
    Vector3 mn = { FLT_MAX, FLT_MAX, FLT_MAX };
    Vector3 mx = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (int i=0;i<8;i++){
        Vector3 p = XformPoint(lightView, cornersWS[i]);
        mn.x = fminf(mn.x, p.x); mn.y = fminf(mn.y, p.y); mn.z = fminf(mn.z, p.z);
        mx.x = fmaxf(mx.x, p.x); mx.y = fmaxf(mx.y, p.y); mx.z = fmaxf(mx.z, p.z);
    }

    // extents and aspect
    float halfW = 0.5f*(mx.x - mn.x);
    float halfH = 0.5f*(mx.y - mn.y);
    const float aspectRT = (float)rtW / (float)rtH;
    float orthoHalfY = fmaxf(halfH, halfW / aspectRT);
    float orthoHalfX = orthoHalfY * aspectRT;

    // center in light space
    Vector3 centerLS = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f };

    // texel snap
    float texelW = (2.0f * orthoHalfX) / (float)shadowRes;
    float texelH = (2.0f * orthoHalfY) / (float)shadowRes;
    float texelWS = fmaxf(texelW, texelH);

    if (kStabilizeShadow && shadowRes > 0) {
        centerLS.x = floorf(centerLS.x / texelW) * texelW;
        centerLS.y = floorf(centerLS.y / texelH) * texelH;
    }

    // reproject snapped center to world
    Matrix invView = MatrixInvert(lightView);
    Vector3 snappedCenterWS = XformPoint(invView, centerLS);

    // final view
    lightView = MatrixLookAt(Vector3Add(snappedCenterWS, Vector3Scale(Vector3Scale(sunDir, -1.0f), 200.0f)), snappedCenterWS, upL);

    // depth range padded - tightened from 50 to 10
    const float zNear = -mx.z - 10.0f;
    const float zFar  = -mn.z + 10.0f;

    Matrix lightProj = MatrixOrtho(-orthoHalfX, +orthoHalfX, -orthoHalfY, +orthoHalfY, zNear, zFar);

    outLightVP = MatrixMultiply(lightView, lightProj);

    // fill Camera3D for BeginMode3D
    outCam = {0};
    outCam.projection = CAMERA_ORTHOGRAPHIC;
    outCam.up = upL;
    outCam.target = snappedCenterWS;
    outCam.position = Vector3Add(snappedCenterWS, Vector3Scale(Vector3Scale(sunDir, -1.0f), 200.0f));
    // raylib's ortho via fovy behaves differently: set fovy to full half-height to match our ortho extents
    outCam.fovy = orthoHalfY * 2.0f;

    outTexelWS = texelWS;
}

// ---------------- Init ----------------
static void EnsureShaders() {
    if (!gLitShader.id) {
        gLitShader = LoadShaderFromMemory(LIT_VS, LIT_FS);

        // lighting
        u_viewPos   = GetShaderLocation(gLitShader, "viewPos");
        u_sunDir    = GetShaderLocation(gLitShader, "sunDir");
        u_sky       = GetShaderLocation(gLitShader, "skyColor");
        u_ground    = GetShaderLocation(gLitShader, "groundColor");
        u_hemi      = GetShaderLocation(gLitShader, "hemiStrength");
        u_sun       = GetShaderLocation(gLitShader, "sunStrength");
        u_ambient   = GetShaderLocation(gLitShader, "ambientColor");
        u_spec      = GetShaderLocation(gLitShader, "specStrength");
        u_shiny     = GetShaderLocation(gLitShader, "shininess");
        u_fresnel   = GetShaderLocation(gLitShader, "fresnelStrength");
        u_aoStrength= GetShaderLocation(gLitShader, "aoStrength");
        u_groundY   = GetShaderLocation(gLitShader, "groundY");

        // cascades
        u_lightVP0  = GetShaderLocation(gLitShader, "lightVP0");
        u_lightVP1  = GetShaderLocation(gLitShader, "lightVP1");
        u_lightVP2  = GetShaderLocation(gLitShader, "lightVP2");
        u_shadowMap0= GetShaderLocation(gLitShader, "shadowMap0");
        u_shadowMap1= GetShaderLocation(gLitShader, "shadowMap1");
        u_shadowMap2= GetShaderLocation(gLitShader, "shadowMap2");
        u_shadowRes = GetShaderLocation(gLitShader, "shadowMapResolution");
        u_pcfStep   = GetShaderLocation(gLitShader, "pcfStep");
        u_biasMin   = GetShaderLocation(gLitShader, "biasMin");
        u_biasMax   = GetShaderLocation(gLitShader, "biasMax");
        u_splits    = GetShaderLocation(gLitShader, "cascadeSplits");
        u_transition = GetShaderLocation(gLitShader, "transitionFrac");

        // normal bias
        u_normalBias0 = GetShaderLocation(gLitShader, "normalBiasWS0");
        u_normalBias1 = GetShaderLocation(gLitShader, "normalBiasWS1");
        u_normalBias2 = GetShaderLocation(gLitShader, "normalBiasWS2");

        // exposure
        u_exposure  = GetShaderLocation(gLitShader, "exposure");

        // constants
        SetShaderValue(gLitShader, u_shadowRes, &kShadowRes, SHADER_UNIFORM_INT);
        float pcfStep = kPCFStep;
        // reduced bias defaults
        float biasMin = 6e-5f, biasMax = 8e-4f;
        SetShaderValue(gLitShader, u_pcfStep, &pcfStep, SHADER_UNIFORM_FLOAT);
        SetShaderValue(gLitShader, u_biasMin, &biasMin, SHADER_UNIFORM_FLOAT);
        SetShaderValue(gLitShader, u_biasMax, &biasMax, SHADER_UNIFORM_FLOAT);

        // set a default exposure in case it's not updated per-frame
        SetShaderValue(gLitShader, u_exposure, &kExposure, SHADER_UNIFORM_FLOAT);

        float defaultTransition = 0.15f; // 15% of split distance -> smooth blend
        SetShaderValue(gLitShader, u_transition, &defaultTransition, SHADER_UNIFORM_FLOAT);
    }

    if (!gLitShaderInst.id) {
        gLitShaderInst = LoadShaderFromMemory(LIT_VS_INST, LIT_FS);

        // in order for DrawMeshInstanced to pick up instanceTransform as the model matrix attribute,
        // assign its attribute location to SHADER_LOC_MATRIX_MODEL
        gLitShaderInst.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(gLitShaderInst, "instanceTransform");

        // lighting (instanced)
        ui_viewPos   = GetShaderLocation(gLitShaderInst, "viewPos");
        ui_sunDir    = GetShaderLocation(gLitShaderInst, "sunDir");
        ui_sky       = GetShaderLocation(gLitShaderInst, "skyColor");
        ui_ground    = GetShaderLocation(gLitShaderInst, "groundColor");
        ui_hemi      = GetShaderLocation(gLitShaderInst, "hemiStrength");
        ui_sun       = GetShaderLocation(gLitShaderInst, "sunStrength");
        ui_ambient   = GetShaderLocation(gLitShaderInst, "ambientColor");
        ui_spec      = GetShaderLocation(gLitShaderInst, "specStrength");
        ui_shiny     = GetShaderLocation(gLitShaderInst, "shininess");
        ui_fresnel   = GetShaderLocation(gLitShaderInst, "fresnelStrength");
        ui_aoStrength= GetShaderLocation(gLitShaderInst, "aoStrength");
        ui_groundY   = GetShaderLocation(gLitShaderInst, "groundY");

        // cascades (instanced)
        ui_lightVP0  = GetShaderLocation(gLitShaderInst, "lightVP0");
        ui_lightVP1  = GetShaderLocation(gLitShaderInst, "lightVP1");
        ui_lightVP2  = GetShaderLocation(gLitShaderInst, "lightVP2");
        ui_shadowMap0= GetShaderLocation(gLitShaderInst, "shadowMap0");
        ui_shadowMap1= GetShaderLocation(gLitShaderInst, "shadowMap1");
        ui_shadowMap2= GetShaderLocation(gLitShaderInst, "shadowMap2");
        ui_shadowRes = GetShaderLocation(gLitShaderInst, "shadowMapResolution");
        ui_pcfStep   = GetShaderLocation(gLitShaderInst, "pcfStep");
        ui_biasMin   = GetShaderLocation(gLitShaderInst, "biasMin");
        ui_biasMax   = GetShaderLocation(gLitShaderInst, "biasMax");
        ui_splits    = GetShaderLocation(gLitShaderInst, "cascadeSplits");
        ui_transition= GetShaderLocation(gLitShaderInst, "transitionFrac");

        // normal bias (instanced)
        ui_normalBias0 = GetShaderLocation(gLitShaderInst, "normalBiasWS0");
        ui_normalBias1 = GetShaderLocation(gLitShaderInst, "normalBiasWS1");
        ui_normalBias2 = GetShaderLocation(gLitShaderInst, "normalBiasWS2");

        // exposure
        ui_exposure  = GetShaderLocation(gLitShaderInst, "exposure");

        // constants
        SetShaderValue(gLitShaderInst, ui_shadowRes, &kShadowRes, SHADER_UNIFORM_INT);
        float pcfStep2 = kPCFStep, biasMin2 = 6e-5f, biasMax2 = 8e-4f;
        SetShaderValue(gLitShaderInst, ui_pcfStep, &pcfStep2, SHADER_UNIFORM_FLOAT);
        SetShaderValue(gLitShaderInst, ui_biasMin, &biasMin2, SHADER_UNIFORM_FLOAT);
        SetShaderValue(gLitShaderInst, ui_biasMax, &biasMax2, SHADER_UNIFORM_FLOAT);
        float defaultTransition2 = 0.15f;
        SetShaderValue(gLitShaderInst, ui_transition, &defaultTransition2, SHADER_UNIFORM_FLOAT);
        SetShaderValue(gLitShaderInst, ui_exposure, &kExposure, SHADER_UNIFORM_FLOAT);
    }

    if (!gSkyShader.id) {
        gSkyShader = LoadShaderFromMemory(SKY_VS, SKY_FS);
        u_inner = GetShaderLocation(gSkyShader, "innerColor");
        u_outer = GetShaderLocation(gSkyShader, "outerColor");
    }

    // Shared cube model for all parts
    if (gPartModel.meshCount == 0) {
        Mesh cubeMesh = GenMeshCube(1.0f, 1.0f, 1.0f);
        gPartModel = LoadModelFromMesh(cubeMesh);
        gPartModel.materials[0].shader = gLitShader; // bind lit+shadow shader for non-instanced path
    }

    // Sky cube
    if (gSkyModel.meshCount == 0) {
        Mesh cubeMesh = GenMeshCube(1.0f, 1.0f, 1.0f);
        gSkyModel = LoadModelFromMesh(cubeMesh);
        gSkyModel.materials[0].shader = gSkyShader;
    }

    // instanced material
    if (!gPartMatInst.shader.id) {
        gPartMatInst = LoadMaterialDefault();
        gPartMatInst.shader = gLitShaderInst;
    }

    for (int i=0;i<3;i++){
        if (!gShadowMapCSM[i].id) {
            gShadowMapCSM[i] = LoadShadowmapRenderTexture(kShadowRes, kShadowRes);
        }
    }
}

// ---------------- Utility: compute cascade splits ----------------
static inline void ComputeCascadeSplits(float n, float f, float lambda, float outSplit[2]){
    const int C = 3;
    for (int i=1;i<=C-1;i++){
        float si = (float)i / (float)C;
        float logD = n * powf(f/n, si);
        float linD = n + (f - n) * si;
        float d = lambda * logD + (1.0f - lambda) * linD;
        outSplit[i-1] = d;
    }
}

// ---------------- Helper: Build instance matrix ----------------
static inline Matrix BuildInstanceMatrix(const CFrame& cf, ::Vector3 size){
    // CFrame::R is row-major 3x3 as used above
    const float m00 = cf.R[0], m01 = cf.R[1], m02 = cf.R[2];
    const float m10 = cf.R[3], m11 = cf.R[4], m12 = cf.R[5];
    const float m20 = cf.R[6], m21 = cf.R[7], m22 = cf.R[8];
    ::Vector3 t = cf.p.toRay();

    Matrix M = {0};
    // column 0 = Sx * R[:,0]
    M.m0 = m00*size.x; M.m1 = m10*size.x; M.m2 = m20*size.x; M.m3 = 0.0f;
    // column 1 = Sy * R[:,1]
    M.m4 = m01*size.y; M.m5 = m11*size.y; M.m6 = m21*size.y; M.m7 = 0.0f;
    // column 2 = Sz * R[:,2]
    M.m8 = m02*size.z; M.m9 = m12*size.z; M.m10= m22*size.z; M.m11= 0.0f;
    // column 3 = translation
    M.m12 = t.x; M.m13 = t.y; M.m14 = t.z; M.m15 = 1.0f;
    return M;
}

// ---------------- Main render ----------------
void RenderFrame(Camera3D& camera) {
    if (IsKeyPressed(KEY_F11)) {
        static bool borderless=false; borderless=!borderless;
        if (borderless) EnterBorderlessFullscreen(); else ExitBorderlessFullscreen();
    }

    EnsureShaders();

    // Camera + culling
    const Vector3 camPos = camera.position;
    Vector3 camDir = Vector3Normalize(Vector3Subtract(camera.target, camPos));
    const float maxDistSq = kMaxDrawDistance * kMaxDrawDistance;
    const float vFov = camera.fovy * (PI/180.0f);
    const float aspect = (float)GetScreenWidth()/(float)GetScreenHeight();
    const float hFov = 2.0f * atanf(tanf(vFov*0.5f)*aspect);
    const float halfCone = 0.5f * ((hFov>vFov)?hFov:vFov) + kFovPaddingDeg*(PI/180.0f);
    const float cosHalf2 = cosf(halfCone)*cosf(halfCone); // kept for reference

    // Lighting params
    Vector3 sunDirV = kUseClockTime
        ? SunDirFromClock(kClockTime)
        : Vector3Normalize(Vector3{1.0f, -1.0f, 1.0f}); // original parity
    float sunDir[3] = { sunDirV.x, sunDirV.y, sunDirV.z };
    float sky[3]    = { 0.60f, 0.70f, 0.90f };
    float ground[3] = { 0.18f, 0.16f, 0.14f };
    float hemiStr   = 0.7f;
    float sunStr    = (kBrightness / 2.0f) * 0.6f;            // default 0.6 identical to original
    float ambient[3]= { kAmbient.r, kAmbient.g, kAmbient.b }; // zero => identical output
    float specStr   = 1.30f;
    float shininess = 128.0f;
    float fresnel   = 0.1f;
    float aoStr     = 0.6f;
    float groundY   = 0.5f;

    // Gather parts
    auto ws = g_game ? g_game->workspace : nullptr;
    struct TItem { std::shared_ptr<Part> p; float dist2; float alpha; };
    std::vector<std::shared_ptr<Part>> opaques;
    std::vector<TItem> transparents;

    if (ws) {
        for (const auto& p : ws->parts) {
            if (!p || !p->Alive) continue;

            // CF position
            Vector3 pos = p->CF.p.toRay();
            Vector3 delta = Vector3Subtract(pos, camPos);
            float d2 = LenSq(delta);
            if (d2 > maxDistSq) continue;

            float dist = sqrtf(d2);
            float cosTheta = Vector3DotProduct(camDir, delta) / (dist > 0 ? dist : 1.0f);

            // size-based bounding sphere radius
            float radius = 0.5f * Vector3Length(p->Size);

            // allow hit if center is inside cone OR sphere overlaps cone boundary
            float angleLimit = cosf(halfCone);
            // if (cosTheta < angleLimit && dist * 0.5f > radius) continue;

            float t = Clamp(p->Transparency, 0.0f, 1.0f);
            float a = 1.0f - t;
            if (a <= 0.0f) continue;
            if (a >= 1.0f) opaques.push_back(p);
            else transparents.push_back({p, d2, a});
        }
    }

    // ---------------- Shadow pass (3 cascades) ----------------
    Camera3D lightCam[3] = {{0},{0},{0}};
    Matrix lightVP[3] = { MatrixIdentity(), MatrixIdentity(), MatrixIdentity() };
    float cascadeTexelWS[3] = {0.0f, 0.0f, 0.0f};

    // compute splits in view-space distance
    float splitRaw[2] = {0.0f, 0.0f};
    ComputeCascadeSplits(kCameraNear, kShadowMaxDistance, kCameraNear==0.0f?0.5f:kCameraNear, splitRaw); // fallback safe call (though original used kCameraNear as near)
    ComputeCascadeSplits(kCameraNear, kShadowMaxDistance, 0.6f, splitRaw);
    float splits[3] = { splitRaw[0], splitRaw[1], kShadowMaxDistance };

    // build and render each cascade
    float nearD = kCameraNear;
    for (int i=0;i<3;i++){
        float farD = splits[i];
        BuildLightCameraForSlice(
            camera, sunDirV,
            nearD, farD,
            kShadowRes,
            gShadowMapCSM[i].texture.width,
            gShadowMapCSM[i].texture.height,
            lightCam[i], lightVP[i],
            cascadeTexelWS[i]
        );
        nearD = farD;
    }

    // Build instance transforms for shadow casters (include opaques and transparents)
    std::vector<Matrix> shadowXforms;
    shadowXforms.reserve(opaques.size() + transparents.size());
    for (auto& p : opaques) shadowXforms.push_back(BuildInstanceMatrix(p->CF, p->Size));
    for (auto& it : transparents) shadowXforms.push_back(BuildInstanceMatrix(it.p->CF, it.p->Size));

    for (int i=0;i<3;i++){
        BeginTextureMode(gShadowMapCSM[i]);
            ClearBackground(WHITE); // depth cleared by BeginMode3D
            BeginMode3D(lightCam[i]);

                // note: rlGetMatrixModelview/projection gives the matrices raylib used; update our lightVP
                Matrix lightView = rlGetMatrixModelview();
                Matrix lightProj = rlGetMatrixProjection();
                lightVP[i] = MatrixMultiply(lightView, lightProj);

                // disable backface culling for shadow pass to reduce acne
                rlDisableBackfaceCulling();

                // Cast shadows from both opaque and transparent geometry (as solid)
                if (!shadowXforms.empty()) {
                    // color doesn't matter; depth-only framebuffer will use depth
                    // ensure instanced material shader is active for drawing instanced meshes
                    gPartMatInst.shader = gLitShaderInst;
                    gPartMatInst.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
                    DrawMeshInstanced(gPartModel.meshes[0], gPartMatInst, shadowXforms.data(), (int)shadowXforms.size());
                }

                // re-enable culling to previous state
                if (kCullBackFace) rlEnableBackfaceCulling();

            EndMode3D();
        EndTextureMode();
    }

    // after building shadow maps, set per-cascade normal-bias based on texel size
    // choose ~1.5 texels of world-space offset as default
    float nb0 = 1.5f * cascadeTexelWS[0];
    float nb1 = 1.5f * cascadeTexelWS[1];
    float nb2 = 1.5f * cascadeTexelWS[2];

    SetShaderValue(gLitShader, u_normalBias0, &nb0, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gLitShader, u_normalBias1, &nb1, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gLitShader, u_normalBias2, &nb2, SHADER_UNIFORM_FLOAT);

    SetShaderValue(gLitShaderInst, ui_normalBias0, &nb0, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gLitShaderInst, ui_normalBias1, &nb1, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gLitShaderInst, ui_normalBias2, &nb2, SHADER_UNIFORM_FLOAT);

    // ---------------- Main pass ----------------
    BeginDrawing();
    ClearBackground(RAYWHITE);
    BeginMode3D(camera);

    // Skybox
    rlDisableDepthTest();
    rlDisableDepthMask();
    bool prevCull = kCullBackFace;
    if (kCullBackFace) rlDisableBackfaceCulling();
    BeginShaderMode(gSkyShader);
        float inner[3] = {0.25f, 0.45f, 0.65f};  // soft horizon blue
        float outer[3] = {0.05f, 0.25f, 0.55f};  // deeper zenith blue
        SetShaderValue(gSkyShader, u_inner, inner, SHADER_UNIFORM_VEC3);
        SetShaderValue(gSkyShader, u_outer, outer, SHADER_UNIFORM_VEC3);
        DrawModel(gSkyModel, {0,0,0}, 100.0f, WHITE);
    EndShaderMode();
    if (kCullBackFace && prevCull) rlEnableBackfaceCulling();
    rlEnableDepthMask();
    rlEnableDepthTest();

    if (kCullBackFace) rlEnableBackfaceCulling();

    // Use Raylib's high-level texture binding instead of low-level OpenGL calls
    // Since we're now using LoadRenderTexture(), use the depth textures properly
    int slot0 = 10, slot1 = 11, slot2 = 12;
    
    // Raylib's SetShaderValueTexture handles texture binding automatically
    // We'll set these in the shader uniform binding section below

    // Prepare per-frame uniform values
    float viewPosArr[3] = { camera.position.x, camera.position.y, camera.position.z };
    float splitVec[3] = { splits[0], splits[1], splits[2] };
    float transitionFrac = 0.15f; // 0.05..0.25 typical; lower = tighter band
    float exposure = kExposure;

    // Helper to set per-frame uniforms for a shader (handles instanced vs non-instanced)
    auto SetPerFrame = [&](const Shader& sh, bool inst){
        // choose location set
        int loc_viewPos    = inst ? ui_viewPos    : u_viewPos;
        int loc_sunDir     = inst ? ui_sunDir     : u_sunDir;
        int loc_sky        = inst ? ui_sky        : u_sky;
        int loc_ground     = inst ? ui_ground     : u_ground;
        int loc_hemi       = inst ? ui_hemi       : u_hemi;
        int loc_sun        = inst ? ui_sun        : u_sun;
        int loc_ambient    = inst ? ui_ambient    : u_ambient;
        int loc_spec       = inst ? ui_spec       : u_spec;
        int loc_shiny      = inst ? ui_shiny      : u_shiny;
        int loc_fresnel    = inst ? ui_fresnel    : u_fresnel;
        int loc_ao         = inst ? ui_aoStrength : u_aoStrength;
        int loc_groundY    = inst ? ui_groundY    : u_groundY;
        int loc_light0     = inst ? ui_lightVP0   : u_lightVP0;
        int loc_light1     = inst ? ui_lightVP1   : u_lightVP1;
        int loc_light2     = inst ? ui_lightVP2   : u_lightVP2;
        int loc_shadow0    = inst ? ui_shadowMap0 : u_shadowMap0;
        int loc_shadow1    = inst ? ui_shadowMap1 : u_shadowMap1;
        int loc_shadow2    = inst ? ui_shadowMap2 : u_shadowMap2;
        int loc_sres       = inst ? ui_shadowRes  : u_shadowRes;
        int loc_pcf        = inst ? ui_pcfStep    : u_pcfStep;
        int loc_biasMin    = inst ? ui_biasMin    : u_biasMin;
        int loc_biasMax    = inst ? ui_biasMax    : u_biasMax;
        int loc_splitsLoc  = inst ? ui_splits     : u_splits;
        int loc_transition = inst ? ui_transition : u_transition;
        int loc_exposureL  = inst ? ui_exposure   : u_exposure;

        SetShaderValue(sh, loc_viewPos, viewPosArr, SHADER_UNIFORM_VEC3);
        SetShaderValue(sh, loc_sunDir, sunDir, SHADER_UNIFORM_VEC3);
        SetShaderValue(sh, loc_sky, sky, SHADER_UNIFORM_VEC3);
        SetShaderValue(sh, loc_ground, ground, SHADER_UNIFORM_VEC3);
        SetShaderValue(sh, loc_hemi, &hemiStr, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, loc_sun, &sunStr, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, loc_ambient, ambient, SHADER_UNIFORM_VEC3);
        SetShaderValue(sh, loc_spec, &specStr, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, loc_shiny, &shininess, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, loc_fresnel, &fresnel, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, loc_ao, &aoStr, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, loc_groundY, &groundY, SHADER_UNIFORM_FLOAT);

        SetShaderValueMatrix(sh, loc_light0, lightVP[0]);
        SetShaderValueMatrix(sh, loc_light1, lightVP[1]);
        SetShaderValueMatrix(sh, loc_light2, lightVP[2]);

        SetShaderValue(sh, loc_splitsLoc, splitVec, SHADER_UNIFORM_VEC3);
        SetShaderValue(sh, loc_transition, &transitionFrac, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, loc_exposureL, &exposure, SHADER_UNIFORM_FLOAT);

        // Use Raylib's high-level texture binding API
        // Bind depth textures for shadow sampling using SetShaderValueTexture
        if (loc_shadow0 >= 0) SetShaderValueTexture(sh, loc_shadow0, gShadowMapCSM[0].depth);
        if (loc_shadow1 >= 0) SetShaderValueTexture(sh, loc_shadow1, gShadowMapCSM[1].depth);
        if (loc_shadow2 >= 0) SetShaderValueTexture(sh, loc_shadow2, gShadowMapCSM[2].depth);
    };

    // Apply per-frame values to both non-instanced and instanced shaders
    SetPerFrame(gLitShader, false);
    SetPerFrame(gLitShaderInst, true);

    // --- Opaques: batch by color -> instanced draws ---
    std::unordered_map<uint32_t, std::vector<Matrix>> batches;
    batches.reserve(64);

    auto pack = [](unsigned r,unsigned g,unsigned b,unsigned a)->uint32_t{
        return (r<<24) | (g<<16) | (b<<8) | a;
    };

    for (auto& p : opaques) {
        Color c = ToRaylibColor(p->Color, 1.0f);
        uint32_t key = pack(c.r,c.g,c.b,c.a);
        batches[key].push_back(BuildInstanceMatrix(p->CF, p->Size));
    }

    // Use instanced material/shader for opaque batches
    if (!batches.empty()) {
        // Ensure instanced material uses our instanced shader
        gPartMatInst.shader = gLitShaderInst;

        for (auto& kv : batches) {
            auto &xforms = kv.second;
            if (xforms.empty()) continue;
            uint32_t key = kv.first;
            Color c = { (unsigned char)(key>>24), (unsigned char)(key>>16), (unsigned char)(key>>8), (unsigned char)(key&0xFF) };
            gPartMatInst.maps[MATERIAL_MAP_DIFFUSE].color = c;
            DrawMeshInstanced(gPartModel.meshes[0], gPartMatInst, xforms.data(), (int)xforms.size());
        }
    }

    // Transparencies (sorted back-to-front)
    std::sort(transparents.begin(), transparents.end(),
              [](const TItem& A, const TItem& B){ return A.dist2 > B.dist2; });
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    for (auto& it : transparents) {
        Color c = ToRaylibColor(it.p->Color, it.alpha);
        Vector3 pos = it.p->CF.p.toRay();
        Vector3 axis; float angleDeg;
        CFrameToAxisAngle(it.p->CF, axis, angleDeg);
        DrawModelEx(gPartModel, pos, axis, angleDeg, it.p->Size, c);
    }
    rlEnableDepthMask();
    EndBlendMode();

    EndShaderMode();

    if (kCullBackFace) rlDisableBackfaceCulling();

    EndMode3D();
    DrawFPS(10,10);
    EndDrawing();
}

// ---------------- Optional cleanup ----------------
void ShutdownRendererShadowResources(){
    for (int i=0;i<3;i++){
        if (gShadowMapCSM[i].id) { UnloadShadowmapRenderTexture(gShadowMapCSM[i]); gShadowMapCSM[i] = {0}; }
    }
    if (gSkyModel.meshCount) { UnloadModel(gSkyModel); gSkyModel = {0}; }
    if (gPartModel.meshCount){ UnloadModel(gPartModel); gPartModel = {0}; }
    if (gPartMatInst.shader.id){ UnloadMaterial(gPartMatInst); gPartMatInst = {0}; }
    if (gSkyShader.id) { UnloadShader(gSkyShader); gSkyShader = {0}; }
    if (gLitShaderInst.id){ UnloadShader(gLitShaderInst); gLitShaderInst = {0}; }
    if (gLitShader.id) { UnloadShader(gLitShader); gLitShader = {0}; }
}
