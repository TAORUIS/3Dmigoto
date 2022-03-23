#include "../util.h"
//#include "Globals.h"
#include "IniHandler.h"
#include "D3D11Wrapper.h"

// Including this after the above headers so the nvapi.h include will pick up
// the d3d11.h pre-processor defines to avoid mis-matches with profiling.hpp:
#include "nvprofile.h"

#include <unordered_set>
#include <fstream>

using namespace std;


ProfileSettings profile_settings;

// Recommended reading:
// NVIDIA Driver Settings Programming Guide
// And the nvapi reference

static wchar_t *exe_path;

// Replacement for GetModuleFileName that will return the passed in filename if
// we are being run as a privileged helper.
static DWORD get_exe_path(wchar_t *filename, DWORD size)
{
    if (exe_path)
        return !wcscpy_s(filename, size, exe_path);

    return GetModuleFileName(0, filename, size);
}

struct NVStereoSetting {
    unsigned id;
    // Setting a flag for known floating point types this since the parsing
    // of floats & ints can be ambiguous (whereas other types are not,
    // though we could use an enum and tag known string types as well).
    // Only setting this on fields identified as floats based on values
    // observed in existing profiles - there may be others:
    bool known_float;
    // This allows a user setting to exist that does not match that
    // specified in the [Profile] section. It is intended primarily for
    // settings that a user may customise on the fly, such as convergence,
    // whether the memo or laser sight is shown, compatibility mode, etc.
    // These settings will still be updated if they are predefined, or when
    // updating the profile for any other reason (the later allows us to
    // force an update if we need to change them by e.g. adding a version
    // number to the Comments setting).
    bool allow_user_customise;
    wchar_t *name;
};

static NVStereoSetting NVStereoSettingNames[] = {
    // Same name we decided to use in nvidia profile inspector:
    {0x701EB457, false, false, L"StereoProfile"},

    // Primary name table:
    {0x70ad05c8, false, false, L"Time"},
    {0x701a8be4, false, false, L"RunTimeName"},
    {0x70cb9168, false, false, L"EnableConsumerStereoSupport"},
    {0x704915a1, false, false, L"StereoViewer"},
    {0x708f9ef7, false, false, L"StereoViewerType"},
    {0x708e5cb4, false, false, L"ShowAllViewerTypes"},
    {0x70538ab1, false, false, L"StereoAdjustEnable"},
    {0x70633bd9, false, false, L"StereoDisableTnL"},
    {0x70c27e3c, false, false, L"StereoTransformationType"},
    {0x70933c00, false,  true, L"StereoSeparation"},
    {0x7082555b, false, false, L"StereoSeparationStep"},
    {0x708db8c5,  true,  true, L"StereoConvergence"},
    {0x708db8c6, false, false, L"StereoVRConvergenceBias"},
    {0x70efbb5b, false, false, L"StereoConvergenceMultiplier"},
    {0x708db8c8, false, false, L"StereoVRRefreshRateOverride"},
    {0x708db8c9, false, false, L"StereoVRVsync"},
    {0x7029432b, false, false, L"RHW2DDetectionMin"},
    {0x702c861a,  true, false, L"RHWGreaterAtScreen"},
    {0x70ab2e09, false, false, L"RHWEqualAtScreen"},
    {0x70381472,  true, false, L"RHWLessAtScreen"},
    {0x702a0ab2, false, false, L"AutoConvergence"},
    {0x70bf3c6b,  true, false, L"AutoConvergenceAdjustPace"},
    {0x70d76b8b, false, false, L"StereoToggle"},
    {0x70121853, false, false, L"SaveStereoImage"},
    {0x7087fe61, false, false, L"StereoVerticalAdjustMore"},
    {0x703acfc6, false, false, L"StereoVerticalAdjustLess"},
    {0x70062f07, false, false, L"StereoHorizontalAdjustMore"},
    {0x70871a39, false, false, L"StereoHorizontalAdjustLess"},
    {0x70ab8d32, false, false, L"StereoSeparationAdjustMore"},
    {0x705d1e02, false, false, L"StereoSeparationAdjustLess"},
    {0x701ed576, false, false, L"StereoConvergenceAdjustMore"},
    {0x70d4add7, false, false, L"StereoConvergenceAdjustLess"},
    {0x70d76b8c, false, false, L"StereoToggleMode"},
    {0x706315af, false, false, L"StereoSuggestSettings"},
    {0x7017861c, false, false, L"StereoUnsuggestSettings"},
    {0x700498b3, false, false, L"WriteConfig"},
    {0x70c73ba2, false, false, L"DeleteConfig"},
    {0x70b7bd1f, false, false, L"ToggleLaserSight"},
    {0x70d8bae6, false, false, L"LaserAdjustXPlus"},
    {0x7048b7dc, false, false, L"LaserAdjustXMinus"},
    {0x7024eda4, false, false, L"LaserAdjustYPlus"},
    {0x70fb9e1e, false, false, L"LaserAdjustYMinus"},
    {0x70085de3, false, false, L"ToggleAutoConvergence"},
    {0x703bc51e, false, false, L"ToggleAutoConvergenceRestore"},
    {0x7066a22e, false, false, L"RHWAtScreenMore"},
    {0x709139ad, false, false, L"RHWAtScreenLess"},
    {0x704e4bca, false, false, L"RHWLessAtScreenMore"},
    {0x70b378a1, false, false, L"RHWLessAtScreenLess"},
    {0x703f4521, false, false, L"GammaAdjustMore"},
    {0x70e8420c, false, false, L"GammaAdjustLess"},
    {0x701fc5b4, false, false, L"GlassesDelayPlus"},
    {0x70b8a743, false, false, L"GlassesDelayMinus"},
    {0x705faed7, false, false, L"FavorSZ"},
    {0x7058b6e1, false, false, L"LaserSight"},
    {0x707ac50d, false, false, L"LaserSightFile"},
    {0x7054837a, false,  true, L"LaserSightEnabled"},
    {0x70da83c6, false, false, L"LaserSightIndex"},
    {0x7032243a, false, false, L"LaserSightProperty"},
    {0x70364596, false, false, L"StereoPointer"},
    {0x702244b7, false, false, L"GameSpecific0"},
    {0x70ab30a7, false, false, L"StereoDefaultOn"},
    {0x70a1411a, false,  true, L"FrustumAdjustMode"},
    {0x7086ebe9, false, false, L"MonitorSize"},
    {0x7032022c, false, false, L"MaxMonitorSize"},
    {0x709e4a94, false, false, L"MaxVertexCount"},
    {0x709794cc, false, false, L"PartialClearMode"},
    {0x7057e831,  true, false, L"LaserXAdjust"},
    {0x70225308,  true, false, L"LaserYAdjust"},
    {0x7014fca2,  true, false, L"LaserZAdjust"},
    {0x702ba385, false, false, L"StereoRefreshDefaultOn"},
    {0x70bd11e0, false, false, L"MixedTnL"},
    {0x70c8b5d1, false, false, L"StereoGamma"},
    {0x70dc4a12, false, false, L"LineCodeColor"},
    {0x70d51cd1, false, false, L"LeftAnaglyphFilter"},
    {0x70f4a930, false, false, L"RightAnaglyphFilter"},
    {0x70b1c8cc, false, false, L"InterleavePattern0"},
    {0x7091a772, false, false, L"InterleavePattern1"},
    {0x70aae185, false, false, L"StereoForceVSync"},
    {0x70e5773b, false, false, L"StereoColorKey"},
    {0x70b17872, false, false, L"ZDirection"},
    {0x70a2000e, false, false, L"StereoCompatibility"},
    {0x70ac6888, false, false, L"LeftColorFilter0"},
    {0x7090b6ca, false, false, L"LeftColorFilter1"},
    {0x70b9a2f7, false, false, L"RightColorFilter0"},
    {0x70aca0cc, false, false, L"RightColorFilter1"},
    {0x706e0041, false, false, L"SharpVPI"},
    {0x701baa09, false, false, L"StereoMode"},
    {0x700a5654, false, false, L"Watchdog"},
    {0x70f455aa, false, false, L"StereoOSDEnable"},
    {0x703564f6, false, false, L"StereoOrthoEnable"},
    {0x70edb381, false, false, L"StereoTextureEnable"},
    {0x709aa171, false, false, L"StereoNotSupported"},
    {0x70969bb0, false, false, L"ModesetWarning"},
    {0x70af6400, false, false, L"StereoFirstTime"},
    {0x70ded3c0, false, false, L"StereoRefreshRate"},
    {0x704a905a, false, false, L"GameConfigs"},
    {0x70729e58, false, false, L"CompareEyes"},
    {0x70efb726, false, false, L"CompareFrom"},
    {0x7097906c, false, false, L"StereoImageType"},
    {0x7004e7a6, false, false, L"SnapShotQuality"},
    {0x7005ad16, false, false, L"NoLockSubstitute"},
    {0x7054fbf8, false, false, L"PushbufSubstituteSize"},
    {0x70175566, false, false, L"DiscardHotkeys"},
    {0x707cfb97, false, false, L"StereoLCDPatternType"},
    {0x70057bb6, false, false, L"GlassesSwitchDelay"},
    {0x7044d7a6, false, false, L"StartZBit"},
    {0x70c71508, false, false, L"DisableOnOutOfMemory"},
    {0x709b3484, false, false, L"StereoWindowedEnable"},
    {0x702c7709, false, false, L"AllowNonExclusiveStereo"},
    {0x706e1913,  true, false, L"Rhwinf"},
    {0x70a4995c,  true, false, L"Rhwscr"},
    {0x70fc13ad, false, false, L"Zinf"}, // float? Only appears once as 0
    {0x707f0e69,  true, false, L"Zscr"},
    {0x7064f0c2, false, false, L"InGameLaserSight"},
    {0x70d1bdb5, false, false, L"CutoffNearDepthLess"},
    {0x7020c991, false, false, L"CutoffNearDepthMore"},
    {0x704c9a46, false, false, L"CutoffFarDepthLess"},
    {0x70fbc04d, false, false, L"CutoffFarDepthMore"},
    {0x704b45c7, false, false, L"CutoffStepLess"},
    {0x700f2971, false, false, L"CutoffStepMore"},
    {0x7050e011,  true, false, L"StereoCutoffDepthNear"},
    {0x70add220,  true, false, L"StereoCutoffDepthFar"},
    {0x709a1ddf, false, false, L"StereoCutoff"},
    {0x702b8c95, false, false, L"EnableCE"},
    {0x70a8fc7f, false, false, L"MediaPlayer"},
    {0x70d10d2b, false, false, L"StereoDX9"},
    {0x70160ebf, false, false, L"StereoMsgVerticalOffset"},
    {0x70031b88, false, false, L"LaserSightTrigger"},
    {0x70bc864d, false, false, L"StereoLaserSightMaxCount"},
    {0x70077042, false, false, L"StereoLaserSightCount"},
    {0x70b6d6ed, false, false, L"StereoEasyZCheck"},
    {0x709bc378, false, false, L"StereoStrictLSCheck"},
    {0x70de5533, false, false, L"StereoDisableAsync"},
    {0x7096eced, false, false, L"EnablePartialStereoBlit"},
    {0x707f4b45, false,  true, L"StereoMemoEnabled"},
    {0x709dea62, false, false, L"StereoNoDepthOverride"},
    {0x702442fc, false, false, L"StereoFlagsDX10"},
    {0x70e34a78, false, false, L"StereoUseMatrix"},
    {0x7044f8fb, false, false, L"StereoShaderMatrixCheck"},
    {0x7052bdd0, false, false, L"StereoLogShaders"},
    {0x70e5a749,  true, false, L"StereoEpsilon"},
    {0x7042eef1, false, false, L"DelayedStereoDesktop"},
    {0x70f8e408, false, false, L"DX10VSCBNumber"},
    {0x70092d4a, false, false, L"DX10DSCBNumber"},
    {0x706139ad, false, false, L"InGameLaserSightDX9States"},
    {0x70ccb5f0, false, false, L"StereoMiscFlags"},
    {0x70e46f20, false, false, L"StereoHiddenProfile"},
    {0x70e46f2a, false, false, L"StereoLinkDll"},
    {0x70e46f2b, false, false, L"EnableStereoCursor"},
    {0x70a7fc7f, false, false, L"CreateStereoDTAfterPresentNum"},
    {0x705fafec, false, false, L"Date_Rel"},
    {0x70c8d48e, false, false, L"Game"},
    {0x709cc5e0, false, false, L"Style"},
    {0x706c7030, false, false, L"Publisher"},
    {0x703c4026, false, false, L"Developer"},
    {0x70b5603f, false, false, L"API"},
    {0x7049c7ec, false, false, L"Value"},
    {0x7051e5f5, false, false, L"Compat"},
    {0x704cde5a, false, false, L"PF_Issues"},
    {0x704d456e, false, false, L"Comments"},
    {0x704f5928, false, false, L"Developer_Issues"},
    {0x70998683, false, false, L"P1SH0"},
    {0x70e6a3cf, false, false, L"V1SH0"},
    {0x7046516e, false, false, L"PSH0"},
    {0x708b7af8, false, false, L"VSH0"},
    {0x708b7af9, false, false, L"VSH1"},
    {0x708b7afa, false, false, L"VSH2"},
    {0x708b7afb, false, false, L"VSH3"},
    {0x708b7afc, false, false, L"VSH4"},
    {0x708b7afd, false, false, L"VSH5"},
    {0x708b7afe, false, false, L"VSH6"},
    {0x708b7aff, false, false, L"VSH7"},
    {0x708b7b00, false, false, L"VSH8"},
    {0x708b7b01, false, false, L"VSH9"},
    {0x708b7b02, false, false, L"VSH10"},
    {0x709adada, false, false, L"2DDHUDSettings"},
    {0x709adadb,  true,  true, L"2DDConvergence"},
    {0x709adadd, false,  true, L"Disable2DD"},
    {0x709adadc, false, false, L"2DD_Notes"},

    // Secondary name table:
    {0x7077bace,  true,  true, L"StereoConvergence (Alternate 1)"},
    {0x7031a2e7, false, false, L"LaserSight (Alternate 1)"},
    {0x70ed1da7, false,  true, L"FrustumAdjustMode (Alternate 1)"},
    {0x70e1518c, false, false, L"StereoTextureEnable (Alternate 1)"},
    {0x70cc286a,  true, false, L"Rhwinf (Alternate 1)"},
    {0x7030b071,  true, false, L"Rhwscr (Alternate 1)"},
    {0x70dd2585, false,  true, L"InGameLaserSight (Alternate 1)"},
    {0x704ef483,  true, false, L"StereoCutoffDepthNear (Alternate 1)"},
    {0x704fcf5c, false, false, L"StereoCutoff (Alternate 1)"},
    {0x7084807e,  true, false, L"StereoConvergence (Alternate 2)"},
    {0x7045b752, false, false, L"LaserSight (Alternate 2)"},
    {0x70f475a0, false,  true, L"FrustumAdjustMode (Alternate 2)"},
    {0x70c0125e, false, false, L"StereoTextureEnable (Alternate 2)"},
    {0x70a3fee6,  true, false, L"Rhwinf (Alternate 2)"},
    {0x70b57ed1,  true, false, L"Rhwscr (Alternate 2)"},
    {0x70e7adad, false,  true, L"InGameLaserSight (Alternate 2)"},
    {0x7031de06,  true, false, L"StereoCutoffDepthNear (Alternate 2)"},
    {0x7053569a, false, false, L"StereoCutoff (Alternate 2)"},
    {0x70f64a32, false, false, L"DX10VSCBNumber (Alternate 2)"},

    // From Nvidia Profile Inspector 2.3.0.2 Reference.xml
    // (None of these return a settingName from the driver):
    {0x0005f511, false, false, L"ASYNC10_ENABLE"},
    {0x0005f512, false, false, L"ASYNC10_PRESENT_MODE"},
    {0x0007fbb6, false, false, L"LARGE_VA"},
    {0x001208cb, false, false, L"FORCEENABLEDCCAP"},
    {0x0013fec2, false, false, L"STAGING_CACHE_SIZE"},
    {0x00159046, false, false, L"PS_BM_DEFER_DESTROY_FLAGS"},
    {0x0015f512, false, false, L"ASYNC9_PRESENT_MODE"},
    {0x00161903, false, false, L"D3D_STEREO_BLACK_LIST"},
    {0x0017fd6d, false, false, L"DISABLE_Z16"},
    {0x002c74f1, false, false, L"3DSMax Shim Layer Control"},
    {0x00313536, false, false, L"PS_ASYNC_SHADER_SCHEDULER_FLAGS"},
    {0x005f5a32, false, false, L"ASYNC10_NVAPI_MODE"},
    {0x005f5a33, false, false, L"ASYNC10_OOO_QUERY_LIMIT"},
    {0x0067345e, false, false, L"D3D9_CONTROL_BITS"},
    {0x006bdd49, false, false, L"PS_SURFACE_PLACEMENT_HIGHEST_PRIORITY"},
    {0x007033ab, false, false, L"KEPLER_CLEAR_SM_HALFCTL_CTRL_SCTL_CYA_6"},
    {0x00837aaf, false, false, L"FERMI_GF110_VOLTAGE_DROP_WAR"},
    {0x00846411, false, false, L"KEPLER_APPLY_ALD_WAR"},
    {0x0085f725, false, false, L"COLORCOMPRESSENABLE"},
    {0x008f7215, false, false, L"READ_ONLY_DSV_EARLYZ_HAZARD_WAR_ENABLE"},
    {0x0090f680, false, false, L"PS_SURFACE_PLACEMENT_ALLOW"},
    {0x0092f4de, false, false, L"ASYNC9_ENABLE"},
    {0x0095def9, false, false, L"SLI Broadcast (DX9)"},
    {0x009cfec8, false, false, L"SLI Copy Engine"},
    {0x009e7462, false, false, L"PS_VB_PROMOTION_FLAGS"},
    {0x00a06746, false, false, L"SLI Specific Hacks (DX1x)"},
    {0x00a0674a, false, false, L"SLI Specific Hacks (DX9)"},
    {0x00a0694b, false, false, L"SLI Broadcast (DX1x)"},
    {0x00a9d3c5, false, false, L"VALIDATE_PARAMS"},
    {0x00b82343, false, false, L"APP_COMPAT_SHIM"}, // Useful - works around threading bugs in RE2/RE7/DMC5
    {0x00bc6199, false, false, L"SHADERMAXREGALLOWED"},
    {0x00c63a79, false, false, L"MAX_PENDING_CMD_BUFFERS"},
    {0x00db3853, false, false, L"SYSMEMCACHEDCOPYBEHAVIOR"},
    {0x00db3857, false, false, L"MCLINKINSTANCED_LIMIT"},
    {0x00db3858, false, false, L"MCFORCEHOSTSTAGINGBUFFERSIZE"},
    {0x00db834a, false, false, L"PS_REDUCTION_LOAD_BALANCED"},
    {0x00dc68b0, false, false, L"BM_OFFER_RECLAIM (DX1x)"},
    {0x00ea83c5, false, false, L"PS_DX10_SURFACE_PLACEMENT_HIGHEST_PRIORITY (DX1x)"},
    {0x00f63a49, false, false, L"WRITE_TO_HOST_STAGING_BUFFER_SIZE"},
    {0x00f63a50, false, false, L"AVOID_HOST_REMAPPERS"},
    {0x00f713bc, false, false, L"SLIMOSAIC_CONTROL"},
    {0x00f865e3, false, false, L"FERMI_FORCE_1X_TEX"},
    {0x00fe8cc0, false, false, L"MAX_RENAMING_MEMORY"},
    {0x00280880, false, false, L"Vertical Sync Indicator"},
    {0x100b8ede, false, false, L"Antialiasing - Combined (Mode + Setting)"},
    {0x102e048d, false, false, L"STEREO_MCCOMPAT"},
    {0x103c2e03, false, false, L"MCSFRLOADBALANCE"},
    {0x10510e76, false, false, L"CULL_BEFORE_FETCH"},
    {0x10528ab2, false, false, L"KEPLER_L1_CACHE_WAR_BUG"},
    {0x106044ce, false, false, L"PS_ALPHABETA"},
    {0x10842563, false, false, L"PS_ALPHABETA_FRACTION"},
    {0x10e3293a, false, false, L"PHYSX_APPLICATION"},
    {0x10f74257, false, false, L"Anisotropic filtering (mode + setting)"},
    {0x10fadc84, false, false, L"ENABLE_CE_MS_HYBRID"},
    {0x11d9dc84, false, false, L"WKS_FEATURE_SUPPORT_CONTROL"},
    {0x2003ff01, false, false, L"OGL_SRV_APP_NON_FSAA_SINGLEBUFFER"},
    {0x2007b25e, false, false, L"OGL_APP_VBUM_VIRTUAL_ADDRESS (XP only)"},
    {0x2014cc70, false, false, L"OGL_SLI_WINDOWED_AFR"},
    {0x201808ac, false, false, L"OGL_SHADER_WARNINGS_AS_ERRORS"},
    {0x201c3b92, false, false, L"OGL_APP_DST_REDUCE"},
    {0x201febd1, false, false, L"OGL_PFD_ADJUST"},
    {0x20219263, false, false, L"OGL_RENDER_QUALITY_FLAGS"},
    {0x2027f0a8, false, false, L"OGL_LDDM_MT_VSYNC_WAIT_METHOD"},
    {0x2027f1a8, false, false, L"OGL_FORCE_FLUSH_TEXTURES"},
    {0x202dc432, false, false, L"OGL_THREAD_OPTIMIZATION_GEFORCE"},
    {0x202de437, false, false, L"OGL_APP_EXPORT_OPENGL_1_2"},
    {0x202fd652, false, false, L"OGL_APP_DIRECT_PUSHBUFFER"},
    {0x202fe114, false, false, L"OGL_APP_RUNTIME_VERTEX_ATTRIB_SOURCING"},
    {0x20320ce4, false, false, L"OGL_APP_YIELD_AFTER_SWAP"},
    {0x20324987, false, false, L"OGL_DX_PRESENT_DEBUG"},
    {0x20334278, false, false, L"OGL_FORCE_NX6PLUSMODE"},
    {0x20335bca, false, false, L"OGL_APP_NO_SW_FALLBACK"},
    {0x20359278, false, false, L"OGL_APP_HIDE_GL3_ON_G80_PLUS"},
    {0x203691bb, false, false, L"OGL_APPLICATION_KEY"},
    {0x203714a4, false, false, L"OGL_PERF_STRAT_LDDM_ALLOCATIONTABLE_SIZE"},
    {0x20378242, false, false, L"OGL_PULL_MEM_BUFFER_SHARED_THRESHOLD"},
    {0x2037f53a, false, false, L"OGL_APP_COPYPIXEL_FRONTBUFFER_FLUSH"},
    {0x203a0751, false, false, L"OGL_OVERLAY_MERGEBLIT_TIMER_MS"},
    {0x203c136f, false, false, L"OGL_APP_PROE_2000I2"},
    {0x203d3fc9, false, false, L"OGL_GLS_COMPATIBILITY"},
    {0x2040b347, false, false, L"OGL_APP_FORCE_FP_DEFAULT_PRECISION"},
    {0x20441369, false, false, L"OGL_APP_PROMOTE_SHADERS_FROM_140_TO_150"},
    {0x2045959a, false, false, L"OGL_CHECK_FOR_IDENTITY_MATRIX_MATH"},
    {0x204b6cbf, false, false, L"OGL_MULTIMON_BUFFER"},
    {0x204cff7b, false, false, L"OGL_TEXCLAMP"},
    {0x20568533, false, false, L"OGL_SRV_APP_NO_PFD_SUPPORT_GDI"},
    {0x20570fc6, false, false, L"OGL_SRV_APP_MEMORY_MAP_NOT_REQUIRED"},
    {0x205f7e3b, false, false, L"OGL_APP_CLAW"},
    {0x205fb5ab, false, false, L"OGL_SRV_APP_FORCE_OGL_GEFORCE"},
    {0x206194d6, false, false, L"OGL_PULL_MEM_DEFAULT_BUFFER_SIZE"},
    {0x20630ba7, false, false, L"OGL_APP_SOFTIMAGE"},
    {0x20696fc3, false, false, L"OGL_APP_LAZY_VBUM_VBOS"},
    {0x206c7691, false, false, L"OGL_APP_HIDE_FLOAT_COLOR_IF_NO_TWO_SIDED"},
    {0x206bd8c7, false, false, L"OGL_APP_PROMOTE_SHADERS_FROM_120_TO_130"},
    {0x20748292, false, false, L"OGL_QUADRO_WORKER_THREAD_CREATION"},
    {0x20748f92, false, false, L"OGL_TREAT_DEPTH24_AS_DEPTH32"},
    {0x207a3a90, false, false, L"OGL_FILTER_PIXELFORMAT"},
    {0x207a8c92, false, false, L"OGL_THREAD_OPTIMIZATION"},
    {0x207c128b, false, false, L"OGL_APP_CENTROID_SAMPLE"},
    {0x207df8d1, false, false, L"OGL_APP_USE_DX10_SAT"},
    {0x2084a0a0, false, false, L"OGL_APP_IGNORE_SWAP_HINTS"},
    {0x20852142, false, false, L"OGL_APP_NO_GENERIC_ARRAYS_IN_FF"},
    {0x20873922, false, false, L"OGL_SLI_DL_CONTROL"},
    {0x208d973f, false, false, L"OGL_SRV_APP_FORCE_SECONDARY_ADAPTER"},
    {0x208f3f2d, false, false, L"OGL_OCGCONTROL_DIRECT_NVIR"},
    {0x2096fd59, false, false, L"OGL_APP_OPTIMIZED_IMMEDIATE_MODE"},
    {0x209746c1, false, false, L"OGL_MULTIGPU_SUPPORT_BITS"}, // Duplicate
    {0x20992431, false, false, L"Prevent Antialiasing (OGL)"},
    {0x209a0ee9, false, false, L"OGL_OVERRIDE_2XTEX"},
    {0x209fd306, false, false, L"OGL_THREAD_CONTROL_2"},
    {0x20a2de5f, false, false, L"OGL_SET_REDUCE_COLOR_THRESHOLDS_UNORM8"},
    {0x20a6eebc, false, false, L"OGL_DISABLE_WORKSTATION_OPTIMIZATIONS"},
    {0x20af671d, false, false, L"OGL_SRV_APP_NOZ16STENCIL"},
    {0x20af9927, false, false, L"OGL_APP_USE_LOOP_ENHANCE"},
    {0x20b36b5c, false, false, L"OGL_FORCE_GAMMA_DOWNSAMPLE"},
    {0x20b3edd5, false, false, L"OGL_APP_CONSTANT_OPTIMIZE"},
    {0x20b812c1, false, false, L"OGL_APP_SET_WGL_COMPATIBILITY_HWND_THREAD_MISMATCH"},
    {0x20bac7fa, false, false, L"OGL_APP_USE_SDDR"},
    {0x20bd755a, false, false, L"OGL_APP_CLAW_THREADS"},
    {0x20bd755b, false, false, L"OGL_APP_CLAW_STATISTICS"},
    {0x20bd755c, false, false, L"OGL_APP_CLAW_OPTIMIZE"},
    {0x20be92cb, false, false, L"OGL_APP_MAXIMIZE_TEXTURE"},
    {0x20c42a03, false, false, L"OGL_APP_NO_TEX_RGBA4_TO_RGBA8_MAPPING"},
    {0x20c618f9, false, false, L"OGL_APP_TXL_LOD_WAR"},
    {0x20cbffc4, false, false, L"OGL_AVOID_VALIDATION_ON_POP_MATRIX_WITHOUT_CHANGE"},
    {0x20d0db0d, false, false, L"OGL_VID_HEAP_REUSE_RATIO"},
    {0x20d324c5, false, false, L"OGL_CONSUMER_API_STEREO_MODE"},
    {0x20d377dc, false, false, L"OGL_APP_VPWW_ALLOW_LATE_ALLOCED"},
    {0x20d38906, false, false, L"OGL_PERF_STRAT_LDDM_CMDBUFFER_SIZE"},
    {0x20d489f4, false, false, L"OGL_APP_SHADER_ASSUMES_0"},
    {0x20d59eda, false, false, L"OGL_APP_VIDMEM_PUSHBUFFER"},
    {0x20d5fdc4, false, false, L"OGL_MULTISAMPLE_COMPATIBILITY"},
    {0x20d6ce24, false, false, L"OGL_SKIP_TEXTURE_HOST_COPIES"},
    {0x20e5c972, false, false, L"OGL_APP_DLIST_SEGMENTING"},
    {0x20e802af, false, false, L"OGL_PSTATE_BOOST_AT_CLEAR"},
    {0x20ea260c, false, false, L"OGL_PERF_STRAT_LDDM_PATCHLOCATIONTABLE_SIZE"},
    {0x20ebd7b8, false, false, L"OGL_APP_SUPPORTBITS2"},
    {0x20ee02b2, false, false, L"OGL_TEARING_FREE_SWAP_PRESENT"},
    {0x20f107cc, false, false, L"OGL_CONFORMANT_INCOMPLETE_TEXTURES"},
    {0x20f2e572, false, false, L"OGL_SYSMEM_TEXTURE_PROMOTION"},
    {0x20f49ae8, false, false, L"OGL_APP_BROKEN_NV3X_DEMOS"},
    {0x20fbbc2b, false, false, L"OGL_DISPATCH_CONTROL"},
    // These are already listed in the secondary stereo name table above
    // with alternate names, but really without understanding the nuances
    // of these it is better to use the ones in the primary name table:
    {0x7031a2e7, false, false, L"STEREO_LASERSIGHT_DX10"},
    {0x704ef483,  true, false, L"STEREO_STEREOCUTOFFDEPTHNEAR_DX10"},
    {0x704fcf5c, false, false, L"STEREO_STEREOCUTOFF_DX10"},
    {0x7053569a, false, false, L"STEREO_STEREOCUTOFF_DX11"},
    {0x7077bace,  true,  true, L"STEREO_STEREOCONVERGENCE_DX10"},
    {0x7084807e,  true,  true, L"STEREO_STEREOCONVERGENCE_DX11"},
    {0x70c0125e, false, false, L"STEREO_STEREOTEXTUREENABLE_DX11"},
    {0x70e1518c, false, false, L"STEREO_STEREOTEXTUREENABLE_DX10"},
    {0x70ed1da7, false,  true, L"STEREO_FRUSTUMADJUSTMODE_DX10"},
    {0x0043ed70, false, false, L"Antialiasing - Transparency Multisampling - Setting"},

    // From Nvidia Profile Inspector 2.3.0.2 CustomSettingNames.xml:
    {0x00a06948, false, false, L"NVLINK SLI Mode"},
    {0x00ce2692, false, false, L"Texture filtering - Quality substitution"},            // Driver name: "Texture filtering - Quality Substitution"
    {0x0098c1ac, false, false, L"Enable Maxwell sample interleaving (MFAA)"},           // Driver name: "Enable sample interleaving (MFAA)"
    {0x0064b541, false, false, L"Prefered Refreshrate"},                                // Driver name: "Preferred refresh rate"
    {0x10029538, false, false, L"GSYNC - Indicator Overlay"},
    {0x1194f158, false, false, L"GSYNC - Application Mode"},                            // Driver name: "Enable G-SYNC globally"
    {0x1094f1f7, false, false, L"GSYNC - Global Mode"},                                 // Driver name: "VRR requested state"
    {0x1094f157, false, false, L"GSYNC - Global Feature"},                              // Driver name: "Toggle the VRR global feature"
    {0x10a879cf, false, false, L"GSYNC - Application State"},                           // Driver name: "G-SYNC"
    {0x10a879ac, false, false, L"GSYNC - Application Requested State"},                 // Driver name: "G-SYNC"
    {0x103bccb5, false, false, L"Prevent Anisotropic filtering"},                       // Driver name: "No override of Anisotropic filtering"
    {0x00198fff, false, false, L"Shadercache"},                                         // Driver name: "Shader Cache"
    {0x000858f7, false, false, L"Antialiasing fix"},
    {0x101ae763, false, false, L"Vertical Sync Smooth AFR behavior"},                   // Driver name: "Flag to control smooth AFR behavior"
    {0x00638e8f, false, false, L"Texture filtering - Driver Controlled LOD Bias"},      // Driver name: "Texture filtering - Driver Controlled LOD Bias"
    {0x002cf156, false, false, L"Flip Indicator"},
    {0x10834fff, false, false, L"Frame Rate Limiter Mode"},                             // Driver name: "Frame Rate Limiter 2 Control"
    {0x10834fee, false, false, L"Frame Rate Limiter"},                                  // Driver name: "Frame Rate Limiter"
    {0x1074c972, false, false, L"Toggle FXAA on or off"},                               // Driver name: "Enable FXAA"
    {0x1068fb9c, false, false, L"Toggle FXAA Indicator on or off"},                     // Driver name: "Enable FXAA Indicator"
    {0x1034cb89, false, false, L"NVIDIA Predefined FXAA Usage"},                        // Driver name: "NVIDIA Predefined FXAA Usage"
    {0x2089bf6c, false, false, L"Antialiasing - Line gamma"},                           // Driver name: "Antialiasing - Line gamma"
    {0x00738e8f, false, false, L"Texture filtering - LOD Bias (DX)"},                   // Driver name: "Texture filtering - LOD Bias"
    {0x20403f79, false, false, L"Texture filtering - LOD Bias (OGL)"},
    {0x209746c1, false, false, L"SLI compatibility bits (OGL)"},                        // Duplicate
    {0x00a04746, false, false, L"SLI compatibility bits (DX12)"},
    {0x00a06946, false, false, L"SLI compatibility bits (DX10 + DX11)"},
    {0x1095def8, false, false, L"SLI compatibility bits"},
    {0x00d55f7d, false, false, L"Antialiasing compatibility"},
    {0x00e32f8a, false, false, L"Antialiasing compatibility (DX1x)"},
    {0x002c7f45, false, false, L"Ambient Occlusion compatibility"},
    {0x107d639d, false, false, L"Antialiasing - Gamma correction"},                     // Driver name: "Antialiasing - Gamma correction"
    {0x10ecdb82, false, false, L"Antialiasing - Behavior Flags"},                       // Driver name: "Antialiasing - Behavior Flags"
    {0x107efc5b, false, false, L"Antialiasing - Mode"},                                 // Driver name: "Antialiasing - Mode"
    {0x10d773d2, false, false, L"Antialiasing - Setting"},                              // Driver name: "Antialiasing - Setting"
    {0x10fc2d9c, false, false, L"Antialiasing - Transparency Multisampling"},           // Driver name: "Antialiasing - Transparency Multisampling"
    {0x10d48a85, false, false, L"Antialiasing - Transparency Supersampling"},           // Driver name: "Antialiasing - Transparency Supersampling"
    {0x10d2bb16, false, false, L"Anisotropic filtering mode"},                          // Driver name: "Anisotropic filtering mode"
    {0x101e61a9, false, false, L"Anisotropic filtering setting"},                       // Driver name: "Anisotropic filtering setting"
    {0x0084cd70, false, false, L"Texture filtering - Anisotropic filter optimization"}, // Driver name: "Texture filtering - Anisotropic filter optimization"
    {0x00e73211, false, false, L"Texture filtering - Anisotropic sample optimization"}, // Driver name: "Texture filtering - Anisotropic sample optimization"
    {0x0019bb68, false, false, L"Texture filtering - Negative LOD bias"},               // Driver name: "Texture filtering - Negative LOD bias"
    {0x00ce2691, false, false, L"Texture filtering - Quality"},                         // Driver name: "Texture filtering - Quality"
    {0x002ecaf2, false, false, L"Texture filtering - Trilinear optimization"},          // Driver name: "Texture filtering - Trilinear optimization"
    {0x00664339, false, false, L"Ambient Occlusion usage"},                             // Driver name: "NVIDIA Predefined Ambient Occlusion Usage"
    {0x00667329, false, false, L"Ambient Occlusion setting"},                           // Driver name: "Ambient Occlusion"
    {0x007ba09e, false, false, L"Maximum pre-rendered frames"},                         // Driver name: "Maximum pre-rendered frames"
    {0x20c1221e, false, false, L"Threaded optimization"},                               // Driver name: "Threaded optimization"
    {0x20fdd1f9, false, false, L"Triple buffering"},                                    // Driver name: "Triple buffering"
    {0x00a879cf, false, false, L"Vertical Sync"},                                       // Driver name: "Vertical Sync"
    {0x005a375c, false, false, L"Vertical Sync Tear Control"},                          // Driver name: "Vertical Sync Tear Control"
    {0x1094f16f, false, false, L"Show PhysX Visual Indicator"},                         // Driver name: "Display the PhysX indicator"
    {0x1057eb71, false, false, L"Power management mode"},                               // Driver name: "Power management mode"
    {0x50166c5e, false, false, L"CUDA - Force P2 State"},
    {0x10c158ad, false, false, L"Optimize for Compute Performance"},                    // Driver name: " " (sic)
    {0x20ff7493, false, false, L"Extension limit"},                                     // Driver name: "Extension String version"
    {0x200aebfc, false, false, L"Multi-display/mixed-GPU acceleration"},
    {0x2046b3ed, false, false, L"OpenGL - Version Override"},
    // Omitting duplicate stereo settings I upstreamed to NPI
};

static wchar_t* lookup_setting_name(unsigned id)
{
    for (int i = 0; i < ARRAYSIZE(NVStereoSettingNames); i++) {
        if (NVStereoSettingNames[i].id == id) {
            return NVStereoSettingNames[i].name;
        }
    }

    return NULL;
}

static int lookup_setting_id(const wchar_t *name, NvU32 *id)
{
    NvAPI_Status status;

    status = NvAPI_DRS_GetSettingIdFromName((NvU16*)name, (NvU32*)id);
    if (status == NVAPI_OK)
        return 0;

    for (int i = 0; i < ARRAYSIZE(NVStereoSettingNames); i++) {
        if (!_wcsicmp(NVStereoSettingNames[i].name, name)) {
            *id = NVStereoSettingNames[i].id;
            return 0;
        }
    }

    return -1;
}

static bool is_known_float_setting(unsigned id)
{
    for (int i = 0; i < ARRAYSIZE(NVStereoSettingNames); i++) {
        if (NVStereoSettingNames[i].id == id) {
            return NVStereoSettingNames[i].known_float;
        }
    }

    return false;
}

static bool is_user_customise_allowed(unsigned id)
{
    for (int i = 0; i < ARRAYSIZE(NVStereoSettingNames); i++) {
        if (NVStereoSettingNames[i].id == id) {
            return NVStereoSettingNames[i].allow_user_customise;
        }
    }

    return false;
}

static void log_nv_error(NvAPI_Status status)
{
    NvAPI_ShortString desc = {};

    if (status == NVAPI_OK)
        return;

    NvAPI_GetErrorMessage(status, desc);
    LOG_INFO("%s\n", desc);
}

static HMODULE nvDLL;
static bool nvapi_failed = false;
typedef NvAPI_Status *(__cdecl *nvapi_QueryInterfaceType)(unsigned int offset);
static nvapi_QueryInterfaceType nvapi_QueryInterfacePtr;
typedef NvAPI_Status(__cdecl *tNvAPI_DRS_SaveSettingsToFileEx)(NvDRSSessionHandle hSession, NvAPI_UnicodeString fileName);
static tNvAPI_DRS_SaveSettingsToFileEx _NvAPI_DRS_SaveSettingsToFileEx;

static NvAPI_Status NvAPI_DRS_SaveSettingsToFileEx(NvDRSSessionHandle hSession, NvAPI_UnicodeString fileName)
{
    if (nvapi_failed)
        return NVAPI_ERROR;

    if (!nvDLL) {
        nvDLL = GetModuleHandle(L"nvapi64.dll");
        if (!nvDLL) {
            nvDLL = GetModuleHandle(L"nvapi.dll");
        }
        if (!nvDLL) {
            LOG_INFO("Can't get nvapi handle\n");
            nvapi_failed = true;
            return NVAPI_ERROR;
        }
    }
    if (!nvapi_QueryInterfacePtr) {
        nvapi_QueryInterfacePtr = (nvapi_QueryInterfaceType)GetProcAddress(nvDLL, "nvapi_QueryInterface");
        LOG_DEBUG("nvapi_QueryInterfacePtr @ 0x%p\n", nvapi_QueryInterfacePtr);
        if (!nvapi_QueryInterfacePtr) {
            LOG_INFO("Unable to call NvAPI_QueryInterface\n");
            nvapi_failed = true;
            return NVAPI_ERROR;
        }
    }
    if (!_NvAPI_DRS_SaveSettingsToFileEx) {
        _NvAPI_DRS_SaveSettingsToFileEx    = (tNvAPI_DRS_SaveSettingsToFileEx)nvapi_QueryInterfacePtr(0x1267818E);
        LOG_DEBUG("NvAPI_DRS_SaveSettingsToFileEx @ 0x%p\n", _NvAPI_DRS_SaveSettingsToFileEx);
        if (!_NvAPI_DRS_SaveSettingsToFileEx) {
            LOG_INFO("Unable to call NvAPI_DRS_SaveSettingsToFileEx\n");
            nvapi_failed = true;
            return NVAPI_ERROR;
        }
    }
    return (*_NvAPI_DRS_SaveSettingsToFileEx)(hSession, fileName);
}

static bool next_broken_utf16_line(std::ifstream *fp, std::wstring *line)
{
    char c[2];

    *line = L"";

    while (fp->good()) {
        fp->read(c, 2);
        if (c[0] == '\r' && c[1] == '\0') {
            fp->read(c, 2);
            if (c[0] == '\n' && c[1] == '\0')
                return true;
            else
                line->push_back(L'\r');
        }
        line->push_back(*(wchar_t*)&c);
    }

    return false;
}

typedef std::unordered_map<wstring, std::unordered_set<unsigned>> internal_setting_map_type;
internal_setting_map_type internal_setting_map;

// Any settings with the internal flag are specially encoded, so we have to
// decode them to be able to make sense of them... but the DRS API does not
// pass this flag to us and they differ for each profile so it seems our only
// option is to ask the driver to save the profiles to disk, and read that back
// in to identify these settings. This is the same approach used by nvidia
// profile inspector.
static int create_internal_setting_map(NvDRSSessionHandle session)
{
    wchar_t tmp[MAX_PATH], path[MAX_PATH];
    NvAPI_Status status = NVAPI_OK;
    wstring line, profile, sid;
    std::ifstream fp;
    unsigned id;

    if (!GetTempPath(MAX_PATH, tmp))
        goto err;

    if (!GetTempFileName(tmp, L"3DM", 0, path))
        goto err;

    status = NvAPI_DRS_SaveSettingsToFileEx(session, (NvU16*)path);
    if (status != NVAPI_OK)
        goto err_rm;

    // We can't treat the file as UTF16, since the encoding scheme they
    // used causes internal strings in the file to violate that encoding
    // and will confuse any parsers (they may stop reading prematurely).
    // Safest thing to do is treat it as a stream of bytes and find the
    // newline characters and strings we are looking for ourselves.
    // Notably, there are potentially edge cases that could screw even this
    // up, but the worst case should only be that we miss one internal
    // string setting... and in practice they fluked avoiding that so far.
    fp.open(path, std::ios::binary);

    while (next_broken_utf16_line(&fp, &line)) {
        if (!line.compare(0, 9, L"Profile \"")) {
            profile = line.substr(9, line.find_last_of(L'\"') - 9);
        }

        if (!line.compare(L"EndProfile")) {
            profile.clear();
        }

        if (!profile.empty() && line.length() > 22 && !line.compare(line.length() - 22, 22, L"InternalSettingFlag=V0")) {
            sid = line.substr(line.find(L" ID_0x") + 6, 8);
            swscanf_s(sid.c_str(), L"%08x", &id);
            // LOG_INFO("Identified Internal Setting ID %S 0x%08x\n", line.c_str(), id);
            internal_setting_map[profile].insert(id);
        }
    }

    fp.close();

    DeleteFile(path);
    return 0;

err_rm:
    DeleteFile(path);
err:
    log_nv_error(status);
    LOG_INFO("WARNING: Unable to determine which settings are internal - some settings will be garbage\n");
    return -1;
}

static void destroy_internal_setting_map()
{
    internal_setting_map.clear();
}

static void _identify_internal_settings(NvDRSSessionHandle session,
        wchar_t *profile_name,
        std::unordered_set<unsigned> **internal_settings)
{
    internal_setting_map_type::iterator i;

    *internal_settings = NULL;

    i = internal_setting_map.find(wstring(profile_name));
    if (i == internal_setting_map.end())
        return;

    *internal_settings = &i->second;
}

static void identify_internal_settings(NvDRSSessionHandle session,
        NvDRSProfileHandle profile,
        std::unordered_set<unsigned> **internal_settings)
{
    NvAPI_Status status = NVAPI_OK;
    NVDRS_PROFILE info = {};

    *internal_settings = NULL;

    info.version = NVDRS_PROFILE_VER;
    status = NvAPI_DRS_GetProfileInfo(session, profile, &info);
    if (status != NVAPI_OK) {
        LOG_INFO_NO_NL("Error getting profile info: ");
        log_nv_error(status);
        return;
    }

    _identify_internal_settings(session, (wchar_t*)info.profileName, internal_settings);
}


unsigned char internal_setting_key[] = {
    0x2f, 0x7c, 0x4f, 0x8b, 0x20, 0x24, 0x52, 0x8d, 0x26, 0x3c, 0x94, 0x77, 0xf3, 0x7c, 0x98, 0xa5,
    0xfa, 0x71, 0xb6, 0x80, 0xdd, 0x35, 0x84, 0xba, 0xfd, 0xb6, 0xa6, 0x1b, 0x39, 0xc4, 0xcc, 0xb0,
    0x7e, 0x95, 0xd9, 0xee, 0x18, 0x4b, 0x9c, 0xf5, 0x2d, 0x4e, 0xd0, 0xc1, 0x55, 0x17, 0xdf, 0x18,
    0x1e, 0x0b, 0x18, 0x8b, 0x88, 0x58, 0x86, 0x5a, 0x1e, 0x03, 0xed, 0x56, 0xfb, 0x16, 0xfe, 0x8a,
    0x01, 0x32, 0x9c, 0x8d, 0xf2, 0xe8, 0x4a, 0xe6, 0x90, 0x8e, 0x15, 0x68, 0xe8, 0x2d, 0xf4, 0x40,
    0x37, 0x9a, 0x72, 0xc7, 0x02, 0x0c, 0xd1, 0xd3, 0x58, 0xea, 0x62, 0xd1, 0x98, 0x36, 0x2b, 0xb2,
    0x16, 0xd5, 0xde, 0x93, 0xf1, 0xba, 0x74, 0xe3, 0x32, 0xc4, 0x9f, 0xf6, 0x12, 0xfe, 0x18, 0xc0,
    0xbb, 0x35, 0x79, 0x9c, 0x6b, 0x7a, 0x23, 0x7f, 0x2b, 0x15, 0x9b, 0x42, 0x07, 0x1a, 0xff, 0x69,
    0xfb, 0x9c, 0xbd, 0x23, 0x97, 0xa8, 0x22, 0x63, 0x8f, 0x32, 0xc8, 0xe9, 0x9b, 0x63, 0x1c, 0xee,
    0x2c, 0xd9, 0xed, 0x8d, 0x3a, 0x35, 0x9c, 0xb1, 0x60, 0xae, 0x5e, 0xf5, 0x97, 0x6b, 0x9f, 0x20,
    0x8c, 0xf7, 0x98, 0x2c, 0x43, 0x79, 0x95, 0x1d, 0xcd, 0x46, 0x36, 0x6c, 0xd9, 0x67, 0x20, 0xab,
    0x41, 0x22, 0x21, 0xe5, 0x55, 0x82, 0xf5, 0x27, 0x20, 0xf5, 0x08, 0x07, 0x3f, 0x6d, 0x69, 0xd9,
    0x1c, 0x4b, 0xf8, 0x26, 0x03, 0x6e, 0xb2, 0x3f, 0x1e, 0xe6, 0xca, 0x3d, 0x61, 0x44, 0xb0, 0x92,
    0xaf, 0xf0, 0x88, 0xca, 0xe0, 0x5f, 0x5d, 0xf4, 0xdf, 0xc6, 0x4c, 0xa4, 0xe0, 0xca, 0xb0, 0x20,
    0x5d, 0xc0, 0xfa, 0xdd, 0x9a, 0x34, 0x8f, 0x50, 0x79, 0x5a, 0x5f, 0x7c, 0x19, 0x9e, 0x40, 0x70,
    0x71, 0xb5, 0x45, 0x19, 0xb8, 0x53, 0xfc, 0xdf, 0x24, 0xbe, 0x22, 0x1c, 0x79, 0xbf, 0x42, 0x89,
};

unsigned decode_internal_dword(unsigned id, unsigned val)
{
    unsigned off, key;

    off = (id << 1);
    key = internal_setting_key[(off+3) % 256] << 24
        | internal_setting_key[(off+2) % 256] << 16
        | internal_setting_key[(off+1) % 256] << 8
        | internal_setting_key[(off+0) % 256];
    // LOG_DEBUG("Decoded Setting ID=%08x Off=%u Key=%08x Enc=%08x Dec=%08x\n", id, off, key, val, key ^ val);
    return key ^ val;
}

void decode_internal_string(unsigned id, NvAPI_UnicodeString val)
{
    unsigned off, i;
    wchar_t key;

    for (i = 0; i < NVAPI_UNICODE_STRING_MAX; i++) {
        off = ((id << 1) + i*2);
        key = internal_setting_key[(off+1) % 256] << 8
            | internal_setting_key[(off+0) % 256];
        // LOG_INFO("Decoded SettingString ID=%08x Off=%u Key=%04x Enc=%04x Dec=%04x\n", id, off, key, val[i], key ^ val[i]);
        val[i] = val[i] ^ key;
        if (!val[i]) // Decoded the NULL terminator
            break;
    }
}

// We aim to make the output of this similar to Geforce Profile Manager so that
// it is familiar, and so that perhaps it could even be copied from one to the
// other, but we definitely do NOT want it to be identical - we want to show
// real setting names, to decode the internal settings, and we definitely don't
// want that horrible encoding nightmare that prevents the file being easily
// opened in anything more sophisticated than notepad!
void _log_nv_profile(NvDRSSessionHandle session, NvDRSProfileHandle profile, NVDRS_PROFILE *info)
{
    std::unordered_set<unsigned> *internal_settings = NULL;
    NvAPI_Status status = NVAPI_OK;
    NVDRS_APPLICATION *apps = NULL;
    NVDRS_SETTING *settings = NULL;
    unsigned len, dval = 0;
    wchar_t *name;
    bool internal;
    NvU32 i;

    info->version = NVDRS_PROFILE_VER;
    status = NvAPI_DRS_GetProfileInfo(session, profile, info);
    if (status != NVAPI_OK)
        goto bail;

    LOG_INFO("Profile \"%S\"%s\n", (wchar_t*)info->profileName,
            info->isPredefined ? "" : " UserSpecified=true");

    if (info->gpuSupport.geforce && info->gpuSupport.quadro) // CHECKME
        LOG_INFO("    ShowOn All\n");
    else if (info->gpuSupport.geforce)
        LOG_INFO("    ShowOn GeForce\n");
    else if (info->gpuSupport.quadro)
        LOG_INFO("    ShowOn Quadro\n");
    if (info->gpuSupport.nvs) // What is this?
        LOG_INFO("    ShowOn NVS\n");

    // XXX: Geforce Profile Manager says "ProfileType Application/Global"...
    // not sure where that comes from

    if (info->numOfApps > 0) {
        apps = new NVDRS_APPLICATION[info->numOfApps];
        memset(apps, 0, sizeof(NVDRS_APPLICATION) * info->numOfApps);
        apps[0].version = NVDRS_APPLICATION_VER;
        status = NvAPI_DRS_EnumApplications(session, profile, 0, &info->numOfApps, apps);
        if (status != NVAPI_OK)
            goto bail;
    }

    for (i = 0; i < info->numOfApps; i++) {
        LOG_INFO_NO_NL("    Executable \"%S\"", (wchar_t*)apps[i].appName);
        if (apps[i].userFriendlyName[0])
            LOG_INFO_NO_NL(" Name=\"%S\"", (wchar_t*)apps[i].userFriendlyName);
        if (apps[i].launcher[0])
            LOG_INFO_NO_NL(" Launcher=\"%S\"", (wchar_t*)apps[i].launcher);
        if (apps[i].fileInFolder[0])
            LOG_INFO_NO_NL(" FindFile=\"%S\"", (wchar_t*)apps[i].fileInFolder);
        if (!apps[i].isPredefined)
            LOG_INFO_NO_NL(" UserSpecified=true");
        LOG_INFO("\n");
        // XXX There's one last piece of info here we might need to
        // output, but I don't see anything that looks like it in the
        // Geforce Profile Manager output: isMetro
    }

    _identify_internal_settings(session, (wchar_t*)info->profileName, &internal_settings);

    if (info->numOfSettings > 0) {
        settings = new NVDRS_SETTING[info->numOfSettings];
        memset(settings, 0, sizeof(NVDRS_SETTING) * info->numOfSettings);
        settings[0].version = NVDRS_SETTING_VER;
        status = NvAPI_DRS_EnumSettings(session, profile, 0, &info->numOfSettings, settings);
        if (status != NVAPI_OK)
            goto bail;
    }

    for (i = 0; i < info->numOfSettings; i++) {
        if (settings[i].settingLocation != NVDRS_CURRENT_PROFILE_LOCATION) {
            // Inherited from base or global profiles, not this one
            continue;
        }

        internal = settings[i].isCurrentPredefined
            && settings[i].isPredefinedValid
            && internal_settings
            && internal_settings->count(settings[i].settingId);

        switch (settings[i].settingType) {
        case NVDRS_DWORD_TYPE:
            dval = settings[i].u32CurrentValue;
            if (internal)
                dval = decode_internal_dword(settings[i].settingId, dval);
            LOG_INFO_NO_NL("    Setting ID_0x%08x = 0x%08x", settings[i].settingId, dval);
            break;
        case NVDRS_BINARY_TYPE:
            // Do these even exist?
            LOG_INFO_NO_NL(" XXX Setting Binary XXX (length=%d) :", settings[i].binaryCurrentValue.valueLength);
            for (len = 0; len < settings[i].binaryCurrentValue.valueLength; len++)
                LOG_INFO_NO_NL(" %02x", settings[i].binaryCurrentValue.valueData[len]);
            break;
        case NVDRS_WSTRING_TYPE:
            if (internal)
                decode_internal_string(settings[i].settingId, settings[i].wszCurrentValue);
            LOG_INFO_NO_NL("    SettingString ID_0x%08x = \"%S\"", settings[i].settingId, (wchar_t*)settings[i].wszCurrentValue);
            break;
        }
        if (!settings[i].isCurrentPredefined)
            LOG_INFO_NO_NL(" UserSpecified=true");
        if (settings[i].settingName[0]) {
            LOG_INFO_NO_NL(" // %S", (wchar_t*)settings[i].settingName);
        } else {
            name = lookup_setting_name(settings[i].settingId);
            if (name) {
                LOG_INFO_NO_NL(" // %S", name);
                // Floating point settings are only in our known table.
                // If we change it to allow floats not in our
                // table, make sure that we have added the //
                // comment character already.
                if (is_known_float_setting(settings[i].settingId))
                    LOG_INFO_NO_NL(" = %.9g", *(float*)&dval);
            }
        }
        LOG_INFO("\n");
    }

    LOG_INFO("EndProfile\n\n");

bail:
    log_nv_error(status);
    delete [] apps;
    delete [] settings;
}

void log_nv_profile(NvDRSSessionHandle session, NvDRSProfileHandle profile)
{
    NVDRS_PROFILE info = {};
    NvAPI_Status status = NVAPI_OK;

    info.version = NVDRS_PROFILE_VER;
    status = NvAPI_DRS_GetProfileInfo(session, profile, &info);
    if (status != NVAPI_OK)
        goto bail;

    return _log_nv_profile(session, profile, &info);

bail:
    log_nv_error(status);
}

void log_all_nv_profiles(NvDRSSessionHandle session)
{
    NvAPI_Status status = NVAPI_OK;
    NvDRSProfileHandle profile = 0;
    unsigned i;

    for (i = 0; (status = NvAPI_DRS_EnumProfiles(session, i, &profile)) == NVAPI_OK; i++)
        log_nv_profile(session, profile);

    if (status != NVAPI_END_ENUMERATION)
        log_nv_error(status);
}

static NvDRSProfileHandle get_cur_nv_profile(NvDRSSessionHandle session)
{
    NvDRSProfileHandle profile = 0;
    NvAPI_Status status = NVAPI_OK;
    NVDRS_APPLICATION app = {};
    wchar_t path[MAX_PATH];

    if (!get_exe_path(path, MAX_PATH)) {
        LOG_INFO("GetModuleFileName failed\n");
        return 0;
    }
    LOG_INFO("\nLooking up profiles related to %S\n", path);

    app.version = NVDRS_APPLICATION_VER;
    status = NvAPI_DRS_FindApplicationByName(session, (NvU16*)path, &profile, &app);
    if (status != NVAPI_OK) {
        LOG_INFO_NO_NL("Cannot locate application profile: ");
        // Not necessarily an error, since the application may not have
        // a profile. Still log the reason:
        log_nv_error(status);
    }

    return profile;
}

// This function logs the contents of all profiles that may have an influence
// on the current game - the base profile, global default profile (if different
// to the base profile), the default stereo profile (or rather will once we
// update our nvapi headers), and
static void log_relevant_nv_profiles(NvDRSSessionHandle session, NvDRSProfileHandle profile)
{
    NvDRSProfileHandle base_profile = 0;
    NvDRSProfileHandle global_profile = 0;
    NvDRSProfileHandle stereo_profile = 0;
    NVDRS_PROFILE base_info = {};
    NVDRS_PROFILE global_info = {};
    NvAPI_Status status = NVAPI_OK;
    NvU32 len = 0;
    char *default_stereo_profile = NULL;

    LOG_INFO("----------- Driver profile settings -----------\n");

    status = NvAPI_DRS_GetBaseProfile(session, &base_profile);
    if (status != NVAPI_OK)
        goto bail;

    base_info.version = NVDRS_PROFILE_VER;
    status = NvAPI_DRS_GetProfileInfo(session, base_profile, &base_info);
    if (status != NVAPI_OK)
        goto bail;

    LOG_INFO("BaseProfile \"%S\"\n", (wchar_t*)base_info.profileName);

    status = NvAPI_DRS_GetCurrentGlobalProfile(session, &global_profile);
    if (status != NVAPI_OK)
        goto bail;

    global_info.version = NVDRS_PROFILE_VER;
    status = NvAPI_DRS_GetProfileInfo(session, global_profile, &global_info);
    if (status != NVAPI_OK)
        goto bail;

    LOG_INFO("SelectedGlobalProfile \"%S\"\n", (wchar_t*)global_info.profileName);

    // TODO: Log current stereo profile
    // FIXME: Update nvapi headers to latest public version to get this function:
    // status = NvAPI_Stereo_GetDefaultProfile(0, 0, &len);
    // if (status != NVAPI_OK)
    //     goto bail;
    // if (len) {
    //     default_stereo_profile = new char[len];
    //     status = NvAPI_Stereo_GetDefaultProfile(len, default_stereo_profile, &len);
    //     if (status != NVAPI_OK)
    //         goto bail;
    //     // Making this look like a comment since Geforce Profile
    //     // Manager does not output it:
    //     LOG_INFO("// Default stereo profile: %s\n", default_stereo_profile);

    // } else {
    //     LOG_INFO("// No default stereo profile set\n");
    // }

    LOG_INFO("\n");

    if (G->dump_all_profiles) {
        log_all_nv_profiles(session);
    } else {
        _log_nv_profile(session, base_profile, &base_info);
        if (base_profile != global_profile)
            _log_nv_profile(session, global_profile, &global_info);

        if (profile)
            log_nv_profile(session, profile);
    }

bail:
    delete default_stereo_profile;
    log_nv_error(status);
    LOG_INFO("----------- End driver profile settings -----------\n");
}

static int parse_ini_profile_lhs(wstring *lhs, NVDRS_SETTING *setting)
{
    int ret, len, off = 0;
    const wchar_t *val = lhs->c_str();

    // Check if the line contains a generic hex setting ID:
    if (!_wcsnicmp(val, L"Setting ID_0x", 13))
        off = 13;
    else if (!_wcsnicmp(val, L"SettingString ID_0x", 19))
        off = 19;
    else if (!_wcsnicmp(val, L"ID_0x", 5))
        off = 5;
    else if (!_wcsnicmp(val, L"0x", 2))
        off = 2;

    if (off) {
        ret = swscanf_s(&val[off], L"%x%n", &setting->settingId, &len);
        if (ret != 0 && ret != EOF && (off + len == lhs->length())) {
            return 0;
        }
    }

    // Finally, try asking the driver and looking up the setting name in
    // our table of Stereo settings:
    return lookup_setting_id(lhs->c_str(), &setting->settingId);
}

static int parse_ini_profile_rhs(wstring *rhs, NVDRS_SETTING *setting)
{
    wstring val(*rhs); // Copy so we still have the original for logging
    bool internal = false;
    wstring::size_type pos;
    unsigned dval;
    int ival, ret, len;
    float fval;

    // XXX: Not sure of best way to handle intermixed quotes and comment
    // characters... blah = "foo // "bar" baz" // "barf". Escape characters
    // are one solution, but I don't want to use them. A regular expression
    // match would make it fairly robust, though the above example is still
    // ambiguous and my experience is that Microsoft's implementation of
    // regexp is buggy, so for now I'll just do something simple and people
    // who use double shashes can break the parsing at their own peril.

    // Strip comments:
    pos = val.rfind(L"//");
    if (pos != val.npos)
        val.resize(pos);

    // Check if it is encrypted:
    pos = val.rfind(L" InternalSettingFlag=V0");
    if (pos != val.npos) {
        val.resize(pos);
        internal = true;
    }

    // Strip the UserSpecified flag (we always set it):
    pos = val.rfind(L" UserSpecified=true");
    if (pos != val.npos)
        val.resize(pos);

    // Strip remaining whitespace
    pos = val.find_last_not_of(L"\t ");
    if (pos + 1 != val.npos)
        val.resize(pos + 1);

    // Check if it looks like a string:
    if (val[0] == L'\"' && val[val.length() - 1] == L'\"') {
        if (val.length() - 2 < NVAPI_UNICODE_STRING_MAX) {
            if (internal) {
                // Ignore encrypted strings, as there is a high
                // probability that they have been corrupted
                // thanks to the bogus encoding they use.
                return -1;
            }
            setting->settingType = NVDRS_WSTRING_TYPE;
            wcscpy_s((wchar_t*)setting->wszCurrentValue, NVAPI_UNICODE_STRING_MAX, val.substr(1, val.length() - 2).c_str());
            return 0;
        }
    }

    // Try parsing value as a hex string:
    ret = swscanf_s(val.c_str(), L"0x%x%n", &dval, &len);
    if (ret != 0 && ret != EOF && len == val.length()) {
        if (internal)
            dval = decode_internal_dword(setting->settingId, dval);
        setting->settingType = NVDRS_DWORD_TYPE;
        setting->u32CurrentValue = dval;
        return 0;
    }

    // Try parsing value as an int:
    ret = swscanf_s(val.c_str(), L"%i%n", &ival, &len);
    if (ret != 0 && ret != EOF && len == val.length()) {
        setting->settingType = NVDRS_DWORD_TYPE;
        if (is_known_float_setting(setting->settingId)) {
            fval = (float)ival;
            setting->u32CurrentValue = *(unsigned*)&fval;
        } else {
            setting->u32CurrentValue = *(unsigned*)&ival;
        }
        return 0;
    }

    // Try parsing value as a float:
    ret = swscanf_s(val.c_str(), L"%f%n", &fval, &len);
    if (ret != 0 && ret != EOF && len == val.length()) {
        setting->settingType = NVDRS_DWORD_TYPE;
        setting->u32CurrentValue = *(unsigned*)&fval;
        return 0;
    }

    return -1;
}

// We want to allow the [Profile] section to be formatted in two ways (and we
// don't care if they are intermixed). We prefer entries like the following:
//
// StereoFlagsDX10 = 0x00004000
// StereoConvergence = 0.5
// Comments = "Boomshakalaka"
//
// But, since people already have profiles saved from Geforce Profile
// Manager, we also would like to accept profiles in that format - even
// decrypting any internal DWORD settings they specify (maybe not encrypted
// strings though - the broken encoding might do horrible things to the ini
// file).
int parse_ini_profile_line(wstring *lhs, wstring *rhs)
{
    NvAPI_Status status = NVAPI_OK;
    NVDRS_SETTING setting = {};

    setting.version = NVDRS_SETTING_VER;

    // Check if it is a line from a Geforce Profile Manager paste we want
    // to ignore:
    if (rhs->empty() && (
            !_wcsnicmp(lhs->c_str(), L"Profile ", 8) ||
            !_wcsnicmp(lhs->c_str(), L"EndProfile", 10) ||
            !_wcsnicmp(lhs->c_str(), L"Executable ", 11) ||
            !_wcsnicmp(lhs->c_str(), L"ProfileType ", 12) ||
            !_wcsnicmp(lhs->c_str(), L"ShowOn ", 7))) {
        LOG_INFO("  Ignoring Line: %S\n", lhs->c_str());
        return 0;
    }

    if (parse_ini_profile_lhs(lhs, &setting)) {
        // Not logging this to the overlay, because I don't know what
        // broken utf16 will do to DirectXTK
        LOG_INFO_NO_NL("  WARNING: Unrecognised line (bad setting): %S = %S", lhs->c_str(), rhs->c_str());
        LOG_INFO("\n"); // In case of utf16 parse error
        beep_sad_failure();
        return -1;
    }

    if (parse_ini_profile_rhs(rhs, &setting)) {
        // Not logging this to the overlay, because I don't know what
        // broken utf16 will do to DirectXTK
        LOG_INFO_NO_NL("  WARNING: Unrecognised line (bad value): %S = %S", lhs->c_str(), rhs->c_str());
        LOG_INFO("\n"); // In case of utf16 parse error
        beep_sad_failure();
        return -1;
    }

    LOG_INFO_NO_NL("  %S (0x%08x) = %S", lhs->c_str(), setting.settingId, rhs->c_str());
    if (setting.settingType == NVDRS_DWORD_TYPE)
        LOG_INFO_NO_NL(" (0x%08x)", setting.u32CurrentValue);
    LOG_INFO("\n");

    // Since the same setting may have several names, duplicates could be
    // missed by the generic ini parsing code. Perform an extra duplicate
    // check here on the setting ID:
    if (profile_settings.count(setting.settingId)) {
        LogOverlayW(LOG_WARNING, L"WARNING: Duplicate driver profile setting ID found in d3dx.ini: 0x%08x\n", setting.settingId);
        // Still doing an audible warning here, because this code path
        // is (also) called from the helper, which has no overlay, and
        // who knows what the result of a bad profile will be:
        beep_sad_failure();
    }

    profile_settings[setting.settingId] = setting;

    return 0;
}

void log_nv_driver_version()
{
    NvU32 version;
    NvAPI_ShortString branch;
    NvAPI_Status status = NVAPI_OK;

    status = NvAPI_SYS_GetDriverAndBranchVersion(&version, branch);
    if (status == NVAPI_OK) {
        LOG_INFO("NVIDIA driver version %u.%u (branch %s)\n", version / 100, version % 100, branch);
    } else {
        LOG_INFO_NO_NL("Error getting NVIDIA driver version: ");
        log_nv_error(status);
    }
}

static int copy_self_to_temp_location(wchar_t *migoto_long_path,
        wchar_t *migoto_short_path,
        wchar_t *d3dcompiler_temp_path)
{
    wchar_t tmp[MAX_PATH];

    if (!GetTempPath(MAX_PATH, d3dcompiler_temp_path))
        return -1;

    if (!GetTempFileName(d3dcompiler_temp_path, L"3DM", 0, migoto_short_path))
        return -1;

    LOG_INFO("Copying %S to %S\n", migoto_long_path, migoto_short_path);
    if (!CopyFile(migoto_long_path, migoto_short_path, false)) {
        LOG_INFO("*** Copy error: %u ***\n", GetLastError());
        goto err_rm;
    }

    // We might also need to copy d3dcompiler_47.dll, since if it cannot be
    // found in any of the standard locations rundll will throw "The
    // specified module could not be found." error. We can avoid this later
    // once we ship our own helper, as we can keep it's dependencies to a
    // minimum:
    wcscat_s(d3dcompiler_temp_path, MAX_PATH, L"d3dcompiler_47.dll");
    wcscpy_s(tmp, MAX_PATH, migoto_long_path);
    wcsrchr(tmp, L'\\')[1] = 0;
    wcscat_s(tmp, MAX_PATH, L"d3dcompiler_47.dll");

    LOG_INFO("Copying %S to %S\n", tmp, d3dcompiler_temp_path);
    if (!CopyFile(tmp, d3dcompiler_temp_path, false)) {
        LOG_INFO("*** Copy error: %u ***\n", GetLastError());
        // Not going to abort in this case - there is a possibility the
        // DLL may be in a standard location and this might still work.
        // If not, we'll get an error dialog and error tones and the
        // user can run the game as admin once to work around it.
    }


    return 0;
err_rm:
    DeleteFile(migoto_short_path);
    return -1;
}

// Raise privileges to allow us to write any changes we need into the driver
// profile.
//
// In order to run some code with admin privileges we need an entire task
// running as admin. This presents a dilemma:
//
// We are a dll loaded by another process, so we cannot ourselves require that
// we are run with admin privileges as that is outside of our control.
//
// We could request the user run the game as admin, but we don't want to do
// that as we don't know what consequences that might have. e.g. the game could
// create a config or save file while running as admin and find itself unable
// to modify (or perhaps even open) that file later when running as a limited
// user.
//
// We could install a privileged service, background task or COM object that we
// communicate with... but we would need to be privileged at some point in
// order to install that. Since we are just dropped in a game directory we
// don't have any opportunity to install such a service.
//
// We could distribute a helper program alongside 3DMigoto that requires admin.
// This is not a bad option, and has the advantage that we should be able to
// set a sensible description that would appear in the UAC prompt, but on the
// downside it would increase the amount of cruft we pollute the game directory
// with, which we want to minimise.
//
// We could distribute the same helper program embedded as a resource inside
// the 3DMigoto dll, which we write to disk and execute only when we need it.
// This might end up being the best option, but at the cost of complexity.
//
// The below is an attempt to use rundll to achieve this - we call rundll with
// runas to request it be run as admin, then have it load us and call back into
// us. One disadvantage of this approach is the description in the UAC dialog
// has no indication of why we need admin, but we could always show our own
// dialog first to explain what we are about to do.
//     MAJOR PROBLEM WITH THIS APPROACH: The full path of the DLL cannot have
//     any spaces or commas in the filename. The documentation recommends using
//     the short path to guarantee this, but if short paths are disabled on a
//     drive this will not work, as seems to be the case when not using drive
//     C. As a workaround we could perhaps copy ourselves to another location,
//     such as the temporary directory - that is less likely to have a space in
//     the path and even if it does it is more likely to have a short filename.
static void spawn_privileged_profile_helper_task()
{
    wchar_t rundll_path[MAX_PATH];
    wchar_t migoto_long_path[MAX_PATH];
    wchar_t migoto_short_path[MAX_PATH];
    wchar_t d3dcompiler_temp_path[MAX_PATH];
    wchar_t game_path[MAX_PATH];
    wstring params;
    SHELLEXECUTEINFO info = {};
    HMODULE module;
    DWORD rc;
    bool do_rm = false;

    if (!GetSystemDirectory(rundll_path, MAX_PATH))
        goto err;
    wcscat(rundll_path, L"\\rundll32.exe");

    // Get a handle to ourselves. Use an address to ensure we get us, not
    // some other d3d11.dll
    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)spawn_privileged_profile_helper_task, &module)) {
        goto err;
    }

    if (!GetModuleFileName(module, migoto_long_path, MAX_PATH))
        goto err;

    // rundll requires the short path to ensure there are no spaces or
    // other punctuation:
    if (!GetShortPathName(migoto_long_path, migoto_short_path, MAX_PATH))
        goto err;

    // If the short path contains a space or a comma (as may happen with
    // e.g. games installed on non system drives that don't have short
    // filenames enabled), we cannot call it with rundll. In that case,
    // copy the dll to a temporary location to increase the chance of
    // success.
    if (wcschr(migoto_short_path, L' ') || wcschr(migoto_short_path, L',')) {
        if (copy_self_to_temp_location(migoto_long_path, migoto_short_path, d3dcompiler_temp_path))
            goto err;
        do_rm = true;

        // If the shortname still has a space, we are boned:
        if (!GetShortPathName(migoto_short_path, migoto_short_path, MAX_PATH))
            goto err;
        if (wcschr(migoto_short_path, L' ') || wcschr(migoto_short_path, L',')) {
            LOG_INFO("*** Temporary directory has a space, cannot use rundll method ***\n");
            goto err;
        }
    }

    // Now we no longer need the long path, turn it into the directory that
    // we can pass to the new process to be able to find the d3dx.ini, even
    // if we are injected from outside the game directory and/or the helper
    // is running out of tmp:
    wcsrchr(migoto_long_path, L'\\')[1] = 0;

    // Since the new process will be rundll, pass in the full path to the
    // game's executable so that we can find the right profile, etc:
    GetModuleFileName(0, game_path, MAX_PATH);

    // Since the parameters include two paths, it could conceivably be
    // longer than MAX_PATH, so use a wstring so we don't need to care:
    params = migoto_short_path;
    params += L",Install3DMigotoDriverProfile ";
    params += game_path;
    params += L"|";
    params += migoto_long_path;

    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = rundll_path;
    info.lpDirectory = migoto_long_path; // rundll seems to ignore this
    info.lpParameters = params.c_str();
    info.nShow = SW_HIDE;

    // ShellExecuteEx may require COM, though will probably be ok without it:
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        LOG_INFO("spawn_privileged_profile_helper_task: CoInitializeEx failed\n");

    LOG_INFO("Spawning helper task to install driver profile...\n");
    LOG_INFO("  Program: %S\n", rundll_path);
    LOG_INFO("  Arguments: %S\n", params.c_str());
    LOG_INFO("  Directory: %S\n", info.lpDirectory);
    if (!ShellExecuteEx(&info))
        goto err;

    if (!info.hProcess)
        goto err;

    if (WaitForSingleObject(info.hProcess, INFINITE) == WAIT_FAILED)
        goto err_close;

    if (!GetExitCodeProcess(info.hProcess, &rc))
        goto err_close;

    LOG_INFO("Helper process terminated with code %u\n", rc);

    CloseHandle(info.hProcess);

    if (do_rm) {
        DeleteFile(migoto_short_path);
        DeleteFile(d3dcompiler_temp_path);
    }

    CoUninitialize();

    return;
err_close:
    CloseHandle(info.hProcess);
err:
    LOG_INFO("Error while requesting admin privileges to install driver profile\n");

    CoUninitialize();

    if (do_rm) {
        DeleteFile(migoto_short_path);
        DeleteFile(d3dcompiler_temp_path);
    }
}

static bool compare_setting(NvDRSSessionHandle session,
        NvDRSProfileHandle profile,
        NVDRS_SETTING *migoto_setting,
        bool allow_user_customisation,
        std::unordered_set<unsigned> *internal_settings,
        bool log)
{
    NVDRS_SETTING driver_setting = {};
    NvAPI_Status status = NVAPI_OK;
    unsigned dval = 0;
    bool internal;

    driver_setting.version = NVDRS_SETTING_VER;
    status = NvAPI_DRS_GetSetting(session, profile, migoto_setting->settingId, &driver_setting);
    if (status != NVAPI_OK) {
        if (log)
            LOG_INFO_NO_NL("Need to update setting 0x%08x because ", migoto_setting->settingId);
        log_nv_error(status);
        return true;
    }

    if (migoto_setting->settingType != driver_setting.settingType) {
        if (log)
            LOG_INFO("Need to update setting 0x%08x because type differs\n", migoto_setting->settingId);
        return true;
    }

    if (allow_user_customisation &&
            !driver_setting.isCurrentPredefined &&
            is_user_customise_allowed(migoto_setting->settingId)) {
        // Allow the user to customise things like convergence
        // without insisting on writing the profile back every
        // launch. We will still update these if we decide we
        // need to update the profile for any other reason.
        return false;
    }

    internal = driver_setting.isCurrentPredefined
        && driver_setting.isPredefinedValid
        && internal_settings
        && internal_settings->count(driver_setting.settingId);

    switch (migoto_setting->settingType) {
    case NVDRS_DWORD_TYPE:
        dval = driver_setting.u32CurrentValue;
        if (internal)
            dval = decode_internal_dword(driver_setting.settingId, dval);
        if (dval != migoto_setting->u32CurrentValue) {
            if (log) {
                LOG_INFO("Need to update DWORD setting 0x%08x from 0x%08x to 0x%08x\n",
                    migoto_setting->settingId, dval,
                    migoto_setting->u32CurrentValue);
            }
            return true;
        }
        break;
    case NVDRS_WSTRING_TYPE:
        if (internal)
            decode_internal_string(driver_setting.settingId, driver_setting.wszCurrentValue);
        if (wcscmp((wchar_t*)driver_setting.wszCurrentValue, (wchar_t*)migoto_setting->wszCurrentValue)) {
            if (log) {
                LOG_INFO("Need to update string setting 0x%08x\n", migoto_setting->settingId);
                LOG_INFO("  From: \"%S\"\n", (wchar_t*)driver_setting.wszCurrentValue);
                LOG_INFO("    To: \"%S\"\n", (wchar_t*)migoto_setting->wszCurrentValue);
            }
            return true;
        }
    }

    return false;
}

static bool need_profile_update(NvDRSSessionHandle session, NvDRSProfileHandle profile)
{
    std::unordered_set<unsigned> *internal_settings = NULL;
    ProfileSettings::iterator i;

    if (profile_settings.empty())
        return false;

    if (profile == 0) {
        LOG_INFO("Need profile update: No profile installed\n");
        return true;
    }

    identify_internal_settings(session, profile, &internal_settings);

    for (i = profile_settings.begin(); i != profile_settings.end(); i++) {
        if (compare_setting(session, profile, &i->second, true, internal_settings, true)) {
            return true;
        }
    }

    return false;
}

static void get_lower_exe_name(wchar_t *name)
{
    wchar_t path[MAX_PATH];
    wchar_t *exe, *c;

    if (!get_exe_path(path, MAX_PATH))
        return;

    exe = &wcsrchr(path, L'\\')[1];
    for (c = exe; *c; c++)
        *c = towlower(*c);

    wcscpy_s(name, NVAPI_UNICODE_STRING_MAX, exe);
}

static void generate_profile_name(wchar_t *name)
{
    wchar_t path[MAX_PATH];
    wchar_t *exe, *ext;

    if (!get_exe_path(path, MAX_PATH))
        return;

    exe = &wcsrchr(path, L'\\')[1];
    ext = wcsrchr(path, L'.');
    if (ext)
        *ext = L'\0';

    wcscpy_s(name, NVAPI_UNICODE_STRING_MAX, exe);
    wcscat_s(name, NVAPI_UNICODE_STRING_MAX, L"-3DMigoto");
}

static int add_exe_to_profile(NvDRSSessionHandle session, NvDRSProfileHandle profile)
{
    NvAPI_Status status = NVAPI_OK;
    NVDRS_APPLICATION app = {};

    app.version = NVDRS_APPLICATION_VER;
    // Could probably add the full path, but then we would be more likely
    // to hit cases where the profile exists but the executable isn't in
    // it. For now just use the executable name unless this proves to be a
    // problem in practice.
    get_lower_exe_name((wchar_t*)app.appName);

    LOG_INFO("Adding \"%S\" to profile\n", (wchar_t*)app.appName);
    status = NvAPI_DRS_CreateApplication(session, profile, &app);
    if (status != NVAPI_OK) {
        LOG_INFO_NO_NL("Error adding app to profile: ");
        log_nv_error(status);
        return -1;
    }

    return 0;
}

static NvDRSProfileHandle create_profile(NvDRSSessionHandle session)
{
    NvAPI_Status status = NVAPI_OK;
    NvDRSProfileHandle profile = 0;
    NVDRS_PROFILE info = {};

    info.version = NVDRS_PROFILE_VER;
    generate_profile_name((wchar_t*)info.profileName);

    // In the event that the profile name already exists, add us to it:
    status = NvAPI_DRS_FindProfileByName(session, info.profileName, &profile);
    if (status == NVAPI_OK) {
        LOG_INFO("Profile \"%S\" already exists\n", (wchar_t*)info.profileName);
        if (add_exe_to_profile(session, profile))
            return 0;
        return profile;
    }

    LOG_INFO("Creating profile \"%S\"\n", (wchar_t*)info.profileName);
    status = NvAPI_DRS_CreateProfile(session, &info, &profile);
    if (status != NVAPI_OK) {
        LOG_INFO_NO_NL("Error creating profile: ");
        log_nv_error(status);
        return 0;
    }

    if (add_exe_to_profile(session, profile))
        return 0;

    return profile;
}

static int update_profile(NvDRSSessionHandle session, NvDRSProfileHandle profile)
{
    std::unordered_set<unsigned> *internal_settings = NULL;
    ProfileSettings::iterator i;
    NvAPI_Status status = NVAPI_OK;
    NVDRS_SETTING *migoto_setting;

    identify_internal_settings(session, profile, &internal_settings);

    for (i = profile_settings.begin(); i != profile_settings.end(); i++) {
        migoto_setting = &i->second;
        if (compare_setting(session, profile, migoto_setting, false, internal_settings, false)) {
            status = NvAPI_DRS_SetSetting(session, profile, migoto_setting);
            if (status != NVAPI_OK) {
                LOG_INFO_NO_NL("Error updating driver profile: ");
                log_nv_error(status);
                return -1;
            }

            switch (migoto_setting->settingType) {
            case NVDRS_DWORD_TYPE:
                LOG_INFO("DWORD setting 0x%08x changed to 0x%08x\n",
                        migoto_setting->settingId,
                        migoto_setting->u32CurrentValue);
                break;
            case NVDRS_WSTRING_TYPE:
                LOG_INFO("String setting 0x%08x changed to \"%S\"\n",
                        migoto_setting->settingId,
                        (wchar_t*)migoto_setting->wszCurrentValue);
                break;
            }
        }
    }

    status = NvAPI_DRS_SaveSettings(session);
    if (status != NVAPI_OK) {
        LOG_INFO_NO_NL("Error saving driver profile: ");
        log_nv_error(status);
        return -1;
    }

    return 0;
}

// rundll entry point - when we come here we should be running in a new process
// with admin privileges, so that we can write any changes we need to the
// driver profile. This follows a subset of the flows that InitializeDLL,
// LoadConfigFile and log_check_and_update_nv_profiles normally do.
void CALLBACK Install3DMigotoDriverProfileW(HWND hwnd, HINSTANCE hinst, LPWSTR lpszCmdLine, int nCmdShow)
{
    NvDRSSessionHandle session = 0;
    NvDRSProfileHandle profile = 0;
    NvAPI_Status status = NVAPI_OK;
    static wchar_t *migoto_dir;

    migoto_dir = wcschr(lpszCmdLine, L'|');
    *(migoto_dir++) = '\0';
    exe_path = lpszCmdLine;

    LoadProfileManagerConfig(migoto_dir);

    // Preload OUR nvapi before we call init because we need some of our calls.
#if(_WIN64)
#define NVAPI_DLL L"nvapi64.dll"
#else
#define NVAPI_DLL L"nvapi.dll"
#endif

    LoadLibrary(NVAPI_DLL);

    // Tell our nvapi.dll that it's us calling, and it's OK.
    nvapi_override();
    status = NvAPI_Initialize();
    if (status != NVAPI_OK) {
        LOG_INFO_NO_NL("  NvAPI_Initialize failed: ");
        log_nv_error(status);
        return;
    }

    log_nv_driver_version();

    status = NvAPI_DRS_CreateSession(&session);
    if (status != NVAPI_OK)
        goto err_no_session;

    status = NvAPI_DRS_LoadSettings(session);
    if (status != NVAPI_OK)
        goto bail;

    if (create_internal_setting_map(session))
        goto bail;

    profile = get_cur_nv_profile(session);

    log_relevant_nv_profiles(session, profile);

    // Don't bother checking if we need to update - if we have been called,
    // that has already been decided.

    if (profile == 0)
        profile = create_profile(session);

    if (profile == 0)
        goto bail;

    if (update_profile(session, profile))
        goto bail;

    LOG_INFO("Profile update successful\n");

bail:
    destroy_internal_setting_map();
    NvAPI_DRS_DestroySession(session);
err_no_session:
    if (status != NVAPI_OK) {
        LOG_INFO_NO_NL("Profile manager error: ");
        log_nv_error(status);
    }
}

void log_check_and_update_nv_profiles()
{
    NvDRSSessionHandle session = 0;
    NvDRSProfileHandle profile = 0;
    NvAPI_Status status = NVAPI_OK;

    // NvAPI_Initialize() should have already been called

    status = NvAPI_DRS_CreateSession(&session);
    if (status != NVAPI_OK)
        goto err_no_session;

    status = NvAPI_DRS_LoadSettings(session);
    if (status != NVAPI_OK)
        goto bail;

    if (create_internal_setting_map(session))
        goto bail;

    profile = get_cur_nv_profile(session);

    log_relevant_nv_profiles(session, profile);

    if (!need_profile_update(session, profile)) {
        LOG_INFO("No profile update required\n");
        goto bail;
    }

    // We need to update the profile. Firstly, we try with our current
    // permissions, just in case we are already running as admin, or nvidia
    // ever decides to allow unprivileged applications to modify their own
    // profile:

    if (profile == 0)
        profile = create_profile(session);

    if (profile == 0 || update_profile(session, profile)) {
        spawn_privileged_profile_helper_task();
    }

    // Close the session, create a new one and log and load the (hopefully)
    // updated settings to see if the helper program was successfully able
    // to update the profile.
    destroy_internal_setting_map();
    NvAPI_DRS_DestroySession(session);

    status = NvAPI_DRS_CreateSession(&session);
    if (status != NVAPI_OK)
        goto err_no_session;

    status = NvAPI_DRS_LoadSettings(session);
    if (status != NVAPI_OK)
        goto bail;

    if (create_internal_setting_map(session))
        goto bail;

    profile = get_cur_nv_profile(session);
    if (profile) {
        LOG_INFO("----------- Updated profile settings -----------\n");
        log_nv_profile(session, profile);
        LOG_INFO("----------- End updated profile settings -----------\n");
    }

    if (need_profile_update(session, profile)) {
        LogOverlay(LOG_DIRE, "WARNING: Profile update failed!\n");
        // Also doing an audible warning to really drive home the point
        // the user was supposed to click OK to that UAC prompt and we
        // are not happy:
        beep_profile_fail();
    }

bail:
    destroy_internal_setting_map();
    NvAPI_DRS_DestroySession(session);
err_no_session:
    if (status != NVAPI_OK) {
        LOG_INFO_NO_NL("Profile manager error: ");
        log_nv_error(status);
    }
}
