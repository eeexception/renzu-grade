// SPDX-License-Identifier: GPL-3.0-only
// An in-process, host-independent contract test. This is not a Resolve emulator.
#include "renzu/Controls.h"
#include "renzu/Match.h"
#include "renzu/Photographic.h"
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <ofxImageEffect.h>
#include <ofxInteract.h>
#include <ofxMultiThread.h>
#include <ofxParam.h>
#include <ofxProperty.h>
#include <stdexcept>
#include <string>
#include <vector>
using namespace renzu;
namespace {
struct TestReferenceLibrary {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("renzu-ofx-reference-test-" +
         std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::string previous;
    bool hadPrevious = false;
    TestReferenceLibrary() {
        if (auto p = std::getenv("RENZU_REFERENCE_DIRECTORY")) {
            previous = p;
            hadPrevious = true;
        }
        std::filesystem::create_directories(root);
        setenv("RENZU_REFERENCE_DIRECTORY", (root / "library").c_str(), 1);
    }
    ~TestReferenceLibrary() {
        if (hadPrevious)
            setenv("RENZU_REFERENCE_DIRECTORY", previous.c_str(), 1);
        else
            unsetenv("RENZU_REFERENCE_DIRECTORY");
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};
struct Props {
    std::map<std::string, std::vector<std::string>> strings;
    std::map<std::string, std::vector<double>> doubles;
    std::map<std::string, std::vector<int>> ints;
    std::map<std::string, std::vector<void *>> pointers;
};
struct Parameter {
    Props props;
    std::string type;
    double d = 0;
    int i = 0;
    std::string s;
    int order = 0;
};
struct Clip {
    Props props;
    Props image;
    bool output = false;
};
struct HostEffect {
    Props props;
    std::map<std::string, Parameter> params;
    std::map<std::string, Clip> clips;
};
OfxPropertySetHandle ph(Props &p) {
    return reinterpret_cast<OfxPropertySetHandle>(&p);
}
Props &props(OfxPropertySetHandle p) {
    return *reinterpret_cast<Props *>(p);
}
HostEffect &fx(OfxImageEffectHandle h) {
    return *reinterpret_cast<HostEffect *>(h);
}
OfxImageEffectHandle eh(HostEffect &h) {
    return reinterpret_cast<OfxImageEffectHandle>(&h);
}
int checks = 0, editBegins = 0, editEnds = 0, fetches = 0, releases = 0, writes = 0;
int parameterOrder = 0;
bool rendering = false, denyButtonFetch = false;
OfxStatus optionalPropertyStatus = kOfxStatOK;
double lastFetch = -1;
void require(bool v, const char *msg) {
    ++checks;
    if (!v)
        throw std::runtime_error(msg);
}
template <class T>
OfxStatus put(std::map<std::string, std::vector<T>> &map, const char *n, int idx, T v) {
    if (idx < 0)
        return kOfxStatErrBadIndex;
    auto &a = map[n];
    a.resize(std::max(a.size(), std::size_t(idx + 1)));
    a[idx] = v;
    return kOfxStatOK;
}
template <class T>
OfxStatus get(const std::map<std::string, std::vector<T>> &map, const char *n, int idx, T *v) {
    auto it = map.find(n);
    if (it == map.end())
        return kOfxStatErrUnknown;
    if (idx < 0 || std::size_t(idx) >= it->second.size())
        return kOfxStatErrBadIndex;
    *v = it->second[idx];
    return kOfxStatOK;
}
OfxStatus ps(OfxPropertySetHandle p, const char *n, int i, const char *v) {
    if (optionalPropertyStatus != kOfxStatOK &&
        (!std::strcmp(n, kOfxPropPluginDescription) ||
         !std::strcmp(n, "OfxImageEffectPropMetalRenderSupported") ||
         !std::strcmp(n, "OfxImageEffectPropMetalTextureSupported")))
        return optionalPropertyStatus;
    return put(props(p).strings, n, i, std::string(v));
}
OfxStatus pi(OfxPropertySetHandle p, const char *n, int i, int v) {
    if (optionalPropertyStatus != kOfxStatOK && !std::strcmp(n, kOfxParamPropGroupOpen))
        return optionalPropertyStatus;
    return put(props(p).ints, n, i, v);
}
OfxStatus pd(OfxPropertySetHandle p, const char *n, int i, double v) {
    return put(props(p).doubles, n, i, v);
}
OfxStatus pp(OfxPropertySetHandle p, const char *n, int i, void *v) {
    return put(props(p).pointers, n, i, v);
}
OfxStatus gs(OfxPropertySetHandle p, const char *n, int i, char **v) {
    std::string s;
    auto r = get(props(p).strings, n, i, &s);
    if (r == kOfxStatOK)
        *v = const_cast<char *>(props(p).strings[n][i].c_str());
    return r;
}
OfxStatus gi(OfxPropertySetHandle p, const char *n, int i, int *v) {
    return get(props(p).ints, n, i, v);
}
OfxStatus gd(OfxPropertySetHandle p, const char *n, int i, double *v) {
    return get(props(p).doubles, n, i, v);
}
OfxStatus gp(OfxPropertySetHandle p, const char *n, int i, void **v) {
    return get(props(p).pointers, n, i, v);
}
OfxStatus gin(OfxPropertySetHandle p, const char *n, int count, int *v) {
    for (int j = 0; j < count; ++j) {
        auto r = gi(p, n, j, v + j);
        if (r)
            return r;
    }
    return kOfxStatOK;
}
OfxStatus gdn(OfxPropertySetHandle p, const char *n, int count, double *v) {
    for (int j = 0; j < count; ++j) {
        auto r = gd(p, n, j, v + j);
        if (r)
            return r;
    }
    return kOfxStatOK;
}
OfxStatus pin(OfxPropertySetHandle p, const char *n, int count, const int *v) {
    for (int j = 0; j < count; ++j)
        pi(p, n, j, v[j]);
    return kOfxStatOK;
}
OfxStatus pdn(OfxPropertySetHandle p, const char *n, int count, const double *v) {
    for (int j = 0; j < count; ++j)
        pd(p, n, j, v[j]);
    return kOfxStatOK;
}
OfxStatus effectProps(OfxImageEffectHandle h, OfxPropertySetHandle *p) {
    *p = ph(fx(h).props);
    return kOfxStatOK;
}
OfxStatus paramSet(OfxImageEffectHandle h, OfxParamSetHandle *p) {
    *p = reinterpret_cast<OfxParamSetHandle>(&fx(h).params);
    return kOfxStatOK;
}
OfxStatus defineParam(OfxParamSetHandle h, const char *type, const char *name,
                      OfxPropertySetHandle *p) {
    auto &m = *reinterpret_cast<std::map<std::string, Parameter> *>(h);
    auto &a = m[name];
    a.type = type;
    a.order = parameterOrder++;
    *p = ph(a.props);
    return kOfxStatOK;
}
OfxStatus paramHandle(OfxParamSetHandle h, const char *n, OfxParamHandle *p,
                      OfxPropertySetHandle *properties) {
    auto &m = *reinterpret_cast<std::map<std::string, Parameter> *>(h);
    auto it = m.find(n);
    if (it == m.end())
        return kOfxStatErrUnknown;
    *p = reinterpret_cast<OfxParamHandle>(&it->second);
    if (properties)
        *properties = ph(it->second.props);
    return kOfxStatOK;
}
OfxStatus readParam(Parameter &p, va_list args) {
    if (p.type == kOfxParamTypeDouble)
        *va_arg(args, double *) = p.d;
    else if (p.type == kOfxParamTypeString)
        *va_arg(args, char **) = const_cast<char *>(p.s.c_str());
    else
        *va_arg(args, int *) = p.i;
    return kOfxStatOK;
}
OfxStatus getParam(OfxParamHandle h, ...) {
    va_list args;
    va_start(args, h);
    auto r = readParam(*reinterpret_cast<Parameter *>(h), args);
    va_end(args);
    return r;
}
OfxStatus getParamTime(OfxParamHandle h, OfxTime time, ...) {
    va_list args;
    va_start(args, time);
    auto r = readParam(*reinterpret_cast<Parameter *>(h), args);
    va_end(args);
    return r;
}
OfxStatus setParam(OfxParamHandle h, ...) {
    require(!rendering, "persistent parameter written during render");
    ++writes;
    auto &p = *reinterpret_cast<Parameter *>(h);
    va_list args;
    va_start(args, h);
    if (p.type == kOfxParamTypeDouble)
        p.d = va_arg(args, double);
    else if (p.type == kOfxParamTypeString)
        p.s = va_arg(args, const char *);
    else
        p.i = va_arg(args, int);
    va_end(args);
    return kOfxStatOK;
}
OfxStatus begin(OfxParamSetHandle, const char *) {
    ++editBegins;
    return kOfxStatOK;
}
OfxStatus end(OfxParamSetHandle) {
    ++editEnds;
    return kOfxStatOK;
}
OfxStatus defineClip(OfxImageEffectHandle h, const char *n, OfxPropertySetHandle *p) {
    auto &c = fx(h).clips[n];
    c.output = std::string(n) == "Output";
    *p = ph(c.props);
    return kOfxStatOK;
}
OfxStatus getClip(OfxImageEffectHandle h, const char *n, OfxImageClipHandle *p,
                  OfxPropertySetHandle *props) {
    auto it = fx(h).clips.find(n);
    if (it == fx(h).clips.end())
        return kOfxStatErrUnknown;
    *p = reinterpret_cast<OfxImageClipHandle>(&it->second);
    if (props)
        *props = ph(it->second.props);
    return kOfxStatOK;
}
OfxStatus fetch(OfxImageClipHandle h, OfxTime time, const OfxRectD *, OfxPropertySetHandle *p) {
    ++fetches;
    lastFetch = time;
    if (denyButtonFetch && !rendering)
        return kOfxStatFailed;
    *p = ph(reinterpret_cast<Clip *>(h)->image);
    return kOfxStatOK;
}
OfxStatus release(OfxPropertySetHandle) {
    ++releases;
    return kOfxStatOK;
}
OfxStatus rod(OfxImageClipHandle, OfxTime, OfxRectD *r) {
    *r = {0, 0, 64, 64};
    return kOfxStatOK;
}
int abortRender(OfxImageEffectHandle) {
    return 0;
}
OfxStatus threads(OfxThreadFunctionV1 *f, unsigned n, void *p) {
    for (unsigned j = 0; j < n; ++j)
        f(j, n, p);
    return kOfxStatOK;
}
OfxStatus cpus(unsigned *n) {
    *n = 4;
    return kOfxStatOK;
}
OfxPropertySuiteV1 propertySuite{};
OfxImageEffectSuiteV1 imageSuite{};
OfxParameterSuiteV1 parameterSuite{};
OfxMultiThreadSuiteV1 threadSuite{};
OfxInteractSuiteV1 interactSuite{};
OfxStatus interactProperties(OfxInteractHandle h, OfxPropertySetHandle *p) {
    *p = reinterpret_cast<OfxPropertySetHandle>(h);
    return kOfxStatOK;
}
const void *suite(OfxPropertySetHandle, const char *n, int version) {
    if (version != 1)
        return nullptr;
    if (!std::strcmp(n, kOfxPropertySuite))
        return &propertySuite;
    if (!std::strcmp(n, kOfxImageEffectSuite))
        return &imageSuite;
    if (!std::strcmp(n, kOfxParameterSuite))
        return &parameterSuite;
    if (!std::strcmp(n, kOfxMultiThreadSuite))
        return &threadSuite;
    if (!std::strcmp(n, kOfxInteractSuite))
        return &interactSuite;
    return nullptr;
}
void initialize() {
    propertySuite.propSetString = ps;
    propertySuite.propSetInt = pi;
    propertySuite.propSetDouble = pd;
    propertySuite.propSetPointer = pp;
    propertySuite.propGetString = gs;
    propertySuite.propGetInt = gi;
    propertySuite.propGetDouble = gd;
    propertySuite.propGetPointer = gp;
    propertySuite.propGetIntN = gin;
    propertySuite.propGetDoubleN = gdn;
    propertySuite.propSetIntN = pin;
    propertySuite.propSetDoubleN = pdn;
    imageSuite.getPropertySet = effectProps;
    imageSuite.getParamSet = paramSet;
    imageSuite.clipDefine = defineClip;
    imageSuite.clipGetHandle = getClip;
    imageSuite.clipGetImage = fetch;
    imageSuite.clipReleaseImage = release;
    imageSuite.clipGetRegionOfDefinition = rod;
    imageSuite.abort = abortRender;
    parameterSuite.paramDefine = defineParam;
    parameterSuite.paramGetHandle = paramHandle;
    parameterSuite.paramGetValue = getParam;
    parameterSuite.paramGetValueAtTime = getParamTime;
    parameterSuite.paramSetValue = setParam;
    parameterSuite.paramEditBegin = begin;
    parameterSuite.paramEditEnd = end;
    threadSuite.multiThread = threads;
    threadSuite.multiThreadNumCPUs = cpus;
    interactSuite.interactGetPropertySet = interactProperties;
}
void defaults(HostEffect &e) {
    for (auto &item : e.params) {
        auto &p = item.second;
        get(p.props.doubles, kOfxParamPropDefault, 0, &p.d);
        get(p.props.ints, kOfxParamPropDefault, 0, &p.i);
        get(p.props.strings, kOfxParamPropDefault, 0, &p.s);
    }
}
void image(Clip &c, float *data) {
    auto p = ph(c.image);
    pp(p, kOfxImagePropData, 0, data);
    int bounds[] = {0, 0, 64, 64};
    pin(p, kOfxImagePropBounds, 4, bounds);
    pi(p, kOfxImagePropRowBytes, 0, 64 * 16);
    ps(p, kOfxImageEffectPropPixelDepth, 0, kOfxBitDepthFloat);
    ps(p, kOfxImageEffectPropComponents, 0, kOfxImageComponentRGBA);
}
} // namespace
int main(int argc, char **argv) {
    try {
        TestReferenceLibrary referenceLibrary;
        require(argc == 2, "plugin path");
        void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!lib)
            throw std::runtime_error(dlerror());
        auto count = (int (*)())dlsym(lib, "OfxGetNumberOfPlugins");
        auto getPlugin = (OfxPlugin * (*)(int)) dlsym(lib, "OfxGetPlugin");
        require(count && getPlugin && count() == 1, "exports");
        auto *plugin = getPlugin(0);
        require(plugin && getPlugin(1) == nullptr && getPlugin(-1) == nullptr,
                "plugin enumeration");
        require(std::string(plugin->pluginIdentifier) == "org.renzu.grade", "stable identifier");
        initialize();
        Props hostProperties;
        pi(ph(hostProperties), kOfxImageEffectPropSupportsOverlays, 0, 1);
        OfxHost host{ph(hostProperties), suite};
        plugin->setHost(&host);
        require(plugin->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr) == kOfxStatOK,
                "load suites");
        // Resolve can reject this optional 1.2 metadata property with ErrUnknown.
        // Discovery must still complete, registering contexts and pixel depths.
        for (OfxStatus unsupported : {kOfxStatErrUnknown, kOfxStatErrUnsupported}) {
            HostEffect limitedDescriptor;
            optionalPropertyStatus = unsupported;
            auto limitedStatus =
                plugin->mainEntry(kOfxActionDescribe, eh(limitedDescriptor), nullptr, nullptr);
            auto limitedContextStatus = plugin->mainEntry(kOfxImageEffectActionDescribeInContext,
                                                          eh(limitedDescriptor), nullptr, nullptr);
            optionalPropertyStatus = kOfxStatOK;
            require(limitedStatus == kOfxStatOK,
                    "unsupported optional properties must not abort discovery");
            require(limitedContextStatus == kOfxStatOK,
                    "unsupported optional group state must not abort context registration");
            require(limitedDescriptor.props.strings[kOfxImageEffectPropSupportedContexts].size() ==
                        2,
                    "contexts registered when optional properties are unavailable");
            require(
                limitedDescriptor.props.strings[kOfxImageEffectPropSupportedPixelDepths].size() ==
                    4,
                "pixel depths registered when optional properties are unavailable");
        }
        HostEffect brokenDescriptor;
        optionalPropertyStatus = kOfxStatErrBadHandle;
        auto brokenStatus =
            plugin->mainEntry(kOfxActionDescribe, eh(brokenDescriptor), nullptr, nullptr);
        optionalPropertyStatus = kOfxStatOK;
        require(brokenStatus == kOfxStatErrBadHandle,
                "genuine property failures must not be silently ignored");
        HostEffect descriptor;
        require(plugin->mainEntry(kOfxActionDescribe, eh(descriptor), nullptr, nullptr) ==
                    kOfxStatOK,
                "describe");
        Props context;
        ps(ph(context), kOfxImageEffectPropContext, 0, kOfxImageEffectContextFilter);
        require(plugin->mainEntry(kOfxImageEffectActionDescribeInContext, eh(descriptor),
                                  ph(context), nullptr) == kOfxStatOK,
                "context");
        HostEffect inst = descriptor;
        const char *uiOrder[] = {"auto",       "whiteBalance", "exposure", "contrastAmount",
                                 "matchGroup", "hdr",          "vibrance", "saturationAmount"};
        for (int j = 1; j < 8; ++j)
            require(descriptor.params.at(uiOrder[j - 1]).order <
                        descriptor.params.at(uiOrder[j]).order,
                    "photographic panel order");
        require(!descriptor.params.count("advanced"), "no Advanced group");
        for (auto n :
             {"patchX", "patchY", "patchRadius", "capture", "applyTone", "applyWB", "applyPatch",
              "saveCapturedReference", "matchCaptured", "matchMode", "space", "pivot"})
            require(descriptor.params.at(n).props.ints[kOfxParamPropSecret][0] == 1,
                    "technical controls hidden");
        for (auto n : {"status", "referenceInfo"})
            require(descriptor.params.at(n).props.ints[kOfxParamPropEnabled][0] == 0,
                    "informational labels cannot be edited");
        require(descriptor.params.at("wbPreset").props.strings[kOfxParamPropChoiceOption] ==
                    std::vector<std::string>{"As Shot", "Auto", "Custom"},
                "only three WB modes");
        for (auto n : {"highlightAmount", "shadowAmount", "whiteAmount", "blackAmount"})
            require(descriptor.params.at(n).props.doubles[kOfxParamPropMin][0] == -100 &&
                        descriptor.params.at(n).props.doubles[kOfxParamPropMax][0] == 100,
                    "photographic HDR ranges");
        defaults(inst);
        inst.params["space"].i = 2;
        for (auto &c : controls) {
            auto &p = inst.params.at(c.id);
            require(p.d == c.value, "native default");
            require(p.props.doubles[kOfxParamPropMin][0] == c.min &&
                        p.props.doubles[kOfxParamPropMax][0] == c.max,
                    "native range");
            require(p.props.ints[kOfxParamPropAnimates][0] == 0, "static whole clip controls");
            require(p.props.ints[kOfxParamPropPersistant][0] == 1, "persistent controls");
        }
        std::vector<float> src(64 * 64 * 4), dst(src.size());
        for (std::size_t k = 0; k < src.size(); k += 4) {
            float v = .01f + .07f * float(k / 4) / 4095;
            src[k] = src[k + 1] = src[k + 2] = v;
            src[k + 3] = .63f;
        }
        image(inst.clips["Source"], src.data());
        image(inst.clips["Output"], dst.data());
        require(plugin->mainEntry(kOfxActionCreateInstance, eh(inst), nullptr, nullptr) ==
                    kOfxStatOK,
                "create instance");
        auto changed = [&](const char *name, double time = 123.5) {
            Props a;
            ps(ph(a), kOfxPropName, 0, name);
            ps(ph(a), kOfxPropType, 0, kOfxTypeParameter);
            ps(ph(a), kOfxPropChangeReason, 0, kOfxChangeUserEdited);
            pd(ph(a), kOfxPropTime, 0, time);
            require(plugin->mainEntry(kOfxActionInstanceChanged, eh(inst), ph(a), nullptr) ==
                        kOfxStatOK,
                    "instance changed");
        };
        auto render = [&](double time) {
            Props a;
            pd(ph(a), kOfxPropTime, 0, time);
            int w[] = {0, 0, 64, 64};
            pin(ph(a), kOfxImageEffectPropRenderWindow, 4, w);
            rendering = true;
            auto status = plugin->mainEntry(kOfxImageEffectActionRender, eh(inst), ph(a), nullptr);
            rendering = false;
            require(status == kOfxStatOK, "render");
        };
        render(123.5);
        require(src == dst, "OFX default identity");
        inst.params["temperature"].d = 12;
        inst.params["saturation"].d = 1.1;
        int before = editBegins;
        changed("autoTone");
        require(lastFetch == 123.5, "selected callback time");
        require(editBegins == before + 1 && editEnds == editBegins, "single undo group");
        require(inst.params["exposure"].d > 0, "Auto Tone writes visible exposure");
        require(inst.params["temperature"].d == 12 && inst.params["saturation"].d == 1.1,
                "Auto Tone independent of color");
        double exposure = inst.params["exposure"].d;
        changed("autoTone");
        require(inst.params["exposure"].d == exposure, "Auto Tone noncompounding");
        inst.params["strength"].d = 0;
        changed("strength");
        require(inst.params["exposure"].d == 0, "zero strength baseline");
        inst.params["strength"].d = 50;
        changed("strength");
        require(std::abs(inst.params["exposure"].d - exposure * .5) < 1.e-8,
                "live strength interpolation");
        // Manual offsets survive subsequent strength changes without private writes
        // during ordinary sliders (including host undo restoration callbacks).
        inst.params["exposure"].d += .1;
        inst.params["strength"].d = 75;
        changed("strength");
        require(std::abs(inst.params["exposure"].d - (exposure * .75 + .1)) < 1.e-8,
                "manual offset survives strength");
        double fixed = inst.params["exposure"].d;
        int originalWrites = writes;
        render(500);
        require(writes == originalWrites && inst.params["exposure"].d == fixed,
                "playback does not change settings");
        for (std::size_t k = 0; k < src.size(); k += 4) {
            src[k] = .24;
            src[k + 1] = .2;
            src[k + 2] = .17;
        }
        changed("autoWB");
        require(inst.params["temperature"].d < 0, "WB writes temperature");
        require(inst.params["exposure"].d == fixed, "WB leaves tone unchanged");
        for (std::size_t k = 0; k < src.size(); k += 4) {
            src[k] = 1;
            src[k + 1] = .01;
            src[k + 2] = .02;
        }
        double wb = inst.params["temperature"].d;
        changed("autoWB");
        require(inst.params["temperature"].d == wb, "unreliable WB preserves settings");
        for (std::size_t k = 0; k < src.size(); k += 4) {
            src[k] = .24;
            src[k + 1] = .2;
            src[k + 2] = .17;
        }
        changed("sample");
        require(inst.params["temperature"].d < 0, "neutral patch applies");
        denyButtonFetch = true;
        changed("autoTone");
        require(inst.params["status"].s.find("Capture Current Frame") != std::string::npos,
                "failed callback explains fallback");
        changed("capture", 44);
        originalWrites = writes;
        render(45);
        require(writes == originalWrites, "unselected frame does not mutate parameters");
        changed("applyTone");
        require(inst.params["status"].s.find("Frame unavailable") != std::string::npos,
                "wrong frame not captured");
        render(44);
        changed("applyWB");
        require(inst.params["temperature"].d < 0, "captured WB applies");
        changed("applyTone");
        require(inst.params["status"].s.find("44.000000") != std::string::npos,
                "captured tone uses requested time");
        // Save the graded source of one effect and match an independent instance.
        denyButtonFetch = false;
        HostEffect sourceClip = descriptor, targetClip = descriptor;
        defaults(sourceClip);
        defaults(targetClip);
        sourceClip.params["space"].i = targetClip.params["space"].i = 2;
        sourceClip.params["temperature"].d = 28;
        sourceClip.params["tint"].d = -11;
        sourceClip.params["exposure"].d = .65;
        sourceClip.params["referenceName"].s = "Камера A";
        std::vector<float> referenceInput(src.size()), targetInput(src.size()),
            referenceOutput(src.size()), targetOutput(src.size());
        for (std::size_t k = 0; k < src.size(); k += 4) {
            float y = .04f + .35f * float(k / 4) / 4095;
            referenceInput[k] = referenceInput[k + 1] = referenceInput[k + 2] = y;
            targetInput[k] = targetInput[k + 1] = targetInput[k + 2] = y * .5f;
            referenceInput[k + 3] = targetInput[k + 3] = .63f;
        }
        image(sourceClip.clips["Source"], referenceInput.data());
        image(sourceClip.clips["Output"], referenceOutput.data());
        image(targetClip.clips["Source"], targetInput.data());
        image(targetClip.clips["Output"], targetOutput.data());
        auto create = [&](HostEffect &clip) {
            require(plugin->mainEntry(kOfxActionCreateInstance, eh(clip), nullptr, nullptr) ==
                        kOfxStatOK,
                    "create reference test instance");
        };
        auto notify = [&](HostEffect &clip, const char *name, double time = 76.5) {
            Props a;
            ps(ph(a), kOfxPropName, 0, name);
            ps(ph(a), kOfxPropType, 0, kOfxTypeParameter);
            ps(ph(a), kOfxPropChangeReason, 0, kOfxChangeUserEdited);
            pd(ph(a), kOfxPropTime, 0, time);
            require(plugin->mainEntry(kOfxActionInstanceChanged, eh(clip), ph(a), nullptr) ==
                        kOfxStatOK,
                    "reference action");
        };
        auto draw = [&](HostEffect &clip, double time) {
            Props a;
            pd(ph(a), kOfxPropTime, 0, time);
            int w[] = {0, 0, 64, 64};
            pin(ph(a), kOfxImageEffectPropRenderWindow, 4, w);
            rendering = true;
            auto status = plugin->mainEntry(kOfxImageEffectActionRender, eh(clip), ph(a), nullptr);
            rendering = false;
            require(status == kOfxStatOK, "reference render");
        };
        auto destroy = [&](HostEffect &clip) {
            require(plugin->mainEntry(kOfxActionDestroyInstance, eh(clip), nullptr, nullptr) ==
                        kOfxStatOK,
                    "destroy reference test instance");
        };
        create(sourceClip);
        create(targetClip);
        before = editBegins;
        notify(sourceClip, "saveReference", 76.5);
        require(editBegins == before + 1 && editEnds == editBegins,
                "save reference has one edit group");
        require(lastFetch == 76.5, "reference saves selected time");
        auto stored = deserializeReference(sourceClip.params["referenceSnapshot"].s);
        require(stored.name == "Камера A" && stored.slot == 1, "named reference saved");
        require(sourceClip.params["reference"].i == 0,
                "new saves keep Latest selected to avoid unintended replacement");
        require(stored.pixels[0].r > referenceInput[0], "reference includes source grade");
        require(targetClip.params["exposure"].d == 0, "saving source does not change target");
        before = editBegins;
        notify(targetClip, "match");
        require(editBegins == before + 1 && editEnds == editBegins, "Match has one undo group");
        require(std::abs(targetClip.params["exposure"].d - 1.65) < .025,
                "other clip receives matched exposure");
        require(std::abs(targetClip.params["temperature"].d - 28) < 1,
                "other clip retains reference warmth");
        require(std::abs(targetClip.params["tint"].d + 11) < 1, "other clip receives matched tint");
        require(targetClip.params["contrast"].d == 1 && targetClip.params["saturation"].d == 1,
                "Match leaves other controls");
        draw(sourceClip, 76.5);
        draw(targetClip, 76.5);
        float maximumDifference = 0;
        bool alphaPreserved = true;
        for (std::size_t k = 0; k < referenceOutput.size(); k += 4) {
            for (int c = 0; c < 3; ++c)
                maximumDifference = std::max(
                    maximumDifference, std::abs(referenceOutput[k + c] - targetOutput[k + c]));
            alphaPreserved &= targetOutput[k + 3] == targetInput[k + 3];
        }
        require(maximumDifference < .002f,
                "matched plugin output pixels agree with graded reference");
        require(alphaPreserved, "Match preserves target alpha");
        double matchedExposure = targetClip.params["exposure"].d,
               matchedTemperature = targetClip.params["temperature"].d;
        notify(targetClip, "match");
        require(std::abs(targetClip.params["exposure"].d - matchedExposure) < 1.e-6,
                "repeated Match noncompounding");
        targetClip.params["matchStrength"].d = 0;
        notify(targetClip, "matchStrength");
        require(targetClip.params["exposure"].d == 0 && targetClip.params["temperature"].d == 0,
                "zero Match strength restores baseline");
        targetClip.params["matchStrength"].d = 50;
        notify(targetClip, "matchStrength");
        require(std::abs(targetClip.params["exposure"].d - matchedExposure * .5) < 1.e-8,
                "Match strength blends EV parameters");
        require(std::abs(targetClip.params["temperature"].d - matchedTemperature * .5) < 1.e-8,
                "Match strength blends WB parameters");
        targetClip.params["exposure"].d += .1;
        targetClip.params["matchStrength"].d = 100;
        notify(targetClip, "matchStrength");
        require(std::abs(targetClip.params["exposure"].d - matchedExposure - .1) < 1.e-8,
                "Match strength preserves manual offset");
        originalWrites = writes;
        draw(targetClip, 1000);
        require(writes == originalWrites, "Match never mutates during playback");
        // Persist the reference in the project. It remains usable without the library.
        auto portable = targetClip.params;
        destroy(targetClip);
        std::filesystem::rename(referenceLibrary.root / "library",
                                referenceLibrary.root / "library-away");
        targetClip = descriptor;
        targetClip.params = portable;
        image(targetClip.clips["Source"], targetInput.data());
        image(targetClip.clips["Output"], targetOutput.data());
        create(targetClip);
        notify(targetClip, "match");
        require(targetClip.params["status"].s.find("Matched Камера A") != std::string::npos,
                "reopened project matches from embedded reference");
        std::filesystem::rename(referenceLibrary.root / "library-away",
                                referenceLibrary.root / "library");
        // A host that refuses callback pixels still supports explicit capture/apply.
        denyButtonFetch = true;
        notify(sourceClip, "capture", 88);
        draw(sourceClip, 89);
        notify(sourceClip, "saveCapturedReference");
        require(sourceClip.params["status"].s.find("Frame unavailable") != std::string::npos,
                "wrong frame cannot become reference");
        draw(sourceClip, 88);
        notify(sourceClip, "saveCapturedReference");
        require(deserializeReference(sourceClip.params["referenceSnapshot"].s).time == 88,
                "captured reference uses requested frame");
        notify(targetClip, "capture", 90);
        originalWrites = writes;
        draw(targetClip, 90);
        require(writes == originalWrites, "capture performs no Match parameter writes");
        notify(targetClip, "matchCaptured");
        require(targetClip.params["status"].s.find("Matched Камера A") != std::string::npos,
                "captured target can match");
        denyButtonFetch = false;
        targetClip.params["reference"].i = 16;
        double unchangedExposure = targetClip.params["exposure"].d;
        notify(targetClip, "reference");
        notify(targetClip, "match");
        require(targetClip.params["exposure"].d == unchangedExposure,
                "missing reference preserves grade");
        HostEffect uiClip = descriptor;
        defaults(uiClip);
        uiClip.params["space"].i = 2;
        image(uiClip.clips["Source"], referenceInput.data());
        image(uiClip.clips["Output"], targetOutput.data());
        create(uiClip);
        uiClip.params["wbTemperature"].d = 5200;
        notify(uiClip, "wbTemperature");
        require(uiClip.params["wbPreset"].i == 2 && uiClip.params["temperature"].d < 0,
                "editing Temp selects Custom and changes WB");
        uiClip.params["wbTint"].d = 19;
        notify(uiClip, "wbTint");
        require(uiClip.params["wbPreset"].i == 2, "editing Tint selects Custom");
        uiClip.params["exposure"].d = .7;
        uiClip.params["wbPreset"].i = 0;
        notify(uiClip, "wbPreset");
        require(uiClip.params["temperature"].d == 0 && uiClip.params["tint"].d == 0 &&
                    uiClip.params["wbTemperature"].d == 6504 && uiClip.params["wbTint"].d == 0,
                "As Shot restores WB defaults");
        require(uiClip.params["exposure"].d == .7, "As Shot preserves exposure");
        uiClip.params["contrastAmount"].d = 30;
        notify(uiClip, "contrastAmount");
        require(std::abs(uiClip.params["contrast"].d - 1.3) < 1.e-8, "contrast percent mapping");
        uiClip.params["highlightAmount"].d = 50;
        notify(uiClip, "highlightAmount");
        require(uiClip.params["lights"].d == 1.5, "Highlights maps bright tonal zone");
        uiClip.params["blackAmount"].d = -40;
        notify(uiClip, "blackAmount");
        require(std::abs(uiClip.params["shadows"].d + 1.2) < 1.e-8, "Blacks maps darkest zone");
        uiClip.params["saturationAmount"].d = -25;
        notify(uiClip, "saturationAmount");
        require(uiClip.params["saturation"].d == .75, "saturation percent mapping");
        before = editBegins;
        notify(uiClip, "auto");
        require(editBegins == before + 1, "full Auto grouped into one undo edit");
        require(uiClip.params["status"].s.find("Auto applied") != std::string::npos,
                "one Auto action computes frame grade");
        // New fallbacks reuse the same primary button, without Capture/Apply controls.
        denyButtonFetch = true;
        double beforeAuto = uiClip.params["exposure"].d;
        notify(uiClip, "auto", 111);
        require(uiClip.params["exposure"].d == beforeAuto,
                "deferred Auto leaves sliders until capture ready");
        originalWrites = writes;
        draw(uiClip, 112);
        require(writes == originalWrites, "wrong frame cannot apply deferred Auto");
        draw(uiClip, 111);
        notify(uiClip, "auto", 111);
        require(uiClip.params["status"].s.find("Auto applied") != std::string::npos,
                "repeat Auto consumes selected-frame capture");
        uiClip.params["referenceName"].s = "UI reference";
        notify(uiClip, "saveReference", 113);
        draw(uiClip, 113);
        notify(uiClip, "saveReference", 113);
        require(deserializeReference(uiClip.params["referenceSnapshot"].s).name == "UI reference",
                "save fallback reuses Save Reference button");
        uiClip.params["matchContrast"].i = 1;
        uiClip.params["matchColor"].i = 1;
        notify(uiClip, "match", 114);
        draw(uiClip, 114);
        notify(uiClip, "match", 114);
        require(uiClip.params["status"].s.find("Matched UI reference") != std::string::npos,
                "Match fallback reuses Match button");
        uiClip.params["wbPreset"].i = 1;
        notify(uiClip, "wbPreset", 116);
        require(uiClip.params["wbPreset"].i == 2, "deferred WB Auto can be selected again");
        draw(uiClip, 116);
        uiClip.params["wbPreset"].i = 1;
        notify(uiClip, "wbPreset", 116);
        require(uiClip.params["wbPreset"].i == 1, "WB Auto dropdown applies prepared frame");
        denyButtonFetch = false;
        // Overlay sampling uses owned pixels, never fetching a host image in an interact.
        notify(uiClip, "pick", 115);
        Props overlayProps;
        pp(ph(overlayProps), kOfxPropEffectInstance, 0, eh(uiClip));
        Props pen;
        pd(ph(pen), kOfxPropTime, 0, 115);
        double point[] = {32, 32};
        pdn(ph(pen), kOfxInteractPropPenPosition, 2, point);
        auto overlayEntry = reinterpret_cast<OfxStatus (*)(
            const char *, const void *, OfxPropertySetHandle, OfxPropertySetHandle)>(
            descriptor.props.pointers[kOfxImageEffectPluginPropOverlayInteractV1][0]);
        int fetchesBeforePicker = fetches;
        require(overlayEntry(kOfxInteractActionPenDown, ph(overlayProps), ph(pen), nullptr) ==
                    kOfxStatOK,
                "viewer picker applies WB in one click");
        require(fetches == fetchesBeforePicker, "viewer interaction never fetches host images");
        require(uiClip.params["pickerArmed"].i == 0 && uiClip.params["wbPreset"].i == 2,
                "picker completes with Custom WB");
        destroy(uiClip);
        destroy(sourceClip);
        destroy(targetClip);
        auto saved = inst.params;
        require(plugin->mainEntry(kOfxActionDestroyInstance, eh(inst), nullptr, nullptr) ==
                    kOfxStatOK,
                "destroy");
        HostEffect reopened = descriptor;
        reopened.params = saved;
        image(reopened.clips["Source"], src.data());
        image(reopened.clips["Output"], dst.data());
        require(plugin->mainEntry(kOfxActionCreateInstance, eh(reopened), nullptr, nullptr) ==
                    kOfxStatOK,
                "reopen");
        require(reopened.params["exposure"].d == saved["exposure"].d &&
                    reopened.params["temperature"].d == saved["temperature"].d,
                "saved host parameters retain grade");
        require(plugin->mainEntry(kOfxActionDestroyInstance, eh(reopened), nullptr, nullptr) ==
                    kOfxStatOK,
                "destroy reopened");
        require(editBegins == editEnds, "balanced edit groups");
        require(releases < fetches, "failed images not released");
        std::cout << "PASS: " << checks
                  << " OpenFX registration, controls, render, selected-frame analysis, "
                     "persistence, undo grouping and capture assertions\n";
        dlclose(lib);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << " after " << checks << " assertions\n";
        return 1;
    }
}
