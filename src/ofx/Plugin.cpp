// SPDX-License-Identifier: GPL-3.0-only
#include "renzu/Controls.h"
#include "renzu/Match.h"
#include "renzu/Photographic.h"
#include <ofxGPURender.h>
#include <ofxImageEffect.h>
#include <ofxInteract.h>
#include <ofxMessage.h>
#include <ofxMultiThread.h>
#include <ofxParam.h>
#include <ofxProperty.h>
#ifdef RENZU_METAL
#include "renzu/Metal.h"
#include <dlfcn.h>
#endif
#include <atomic>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
namespace {
using namespace renzu;
OfxHost *host = nullptr;
OfxPropertySuiteV1 *prop = nullptr;
OfxImageEffectSuiteV1 *effect = nullptr;
OfxParameterSuiteV1 *param = nullptr;
OfxMultiThreadSuiteV1 *threading = nullptr;
OfxInteractSuiteV1 *interact = nullptr;
struct Failure {
    OfxStatus status;
};
void check(OfxStatus s) {
    if (s != kOfxStatOK)
        throw Failure{s};
}
void stringSet(OfxPropertySetHandle p, const char *n, const char *v, int i = 0) {
    check(prop->propSetString(p, n, i, v));
}
void optionalStringSet(OfxPropertySetHandle p, const char *n, const char *v) {
    // Hosts may omit newer metadata/extension properties. This must not prevent
    // the mandatory contexts and depths from being registered during Describe.
    OfxStatus status = prop->propSetString(p, n, 0, v);
    if (status != kOfxStatErrUnknown && status != kOfxStatErrUnsupported)
        check(status);
}
void intSet(OfxPropertySetHandle p, const char *n, int v, int i = 0) {
    check(prop->propSetInt(p, n, i, v));
}
void optionalIntSet(OfxPropertySetHandle p, const char *n, int v) {
    OfxStatus status = prop->propSetInt(p, n, 0, v);
    if (status != kOfxStatErrUnknown && status != kOfxStatErrUnsupported)
        check(status);
}
void doubleSet(OfxPropertySetHandle p, const char *n, double v, int i = 0) {
    check(prop->propSetDouble(p, n, i, v));
}
std::string getString(OfxPropertySetHandle p, const char *n, const char *f = "") {
    char *v = nullptr;
    return p && prop->propGetString(p, n, 0, &v) == kOfxStatOK && v ? v : f;
}
int getInt(OfxPropertySetHandle p, const char *n, int fallback = 0) {
    int v = fallback;
    if (p)
        prop->propGetInt(p, n, 0, &v);
    return v;
}
double getDouble(OfxPropertySetHandle p, const char *n, double fallback = 0) {
    double v = fallback;
    if (p)
        prop->propGetDouble(p, n, 0, &v);
    return v;
}
void *getPointer(OfxPropertySetHandle p, const char *n) {
    void *v = nullptr;
    if (p)
        prop->propGetPointer(p, n, 0, &v);
    return v;
}
OfxPropertySetHandle properties(OfxImageEffectHandle h) {
    OfxPropertySetHandle p;
    check(effect->getPropertySet(h, &p));
    return p;
}
struct Capture {
    bool ready = false;
    double time = 0;
    std::vector<V3> pixels;
    WBResult patch;
    Settings settings;
    std::vector<float> picker;
    int pickerWidth = 0, pickerHeight = 0;
};
struct Instance {
    OfxImageEffectHandle handle;
    OfxParamSetHandle parameters;
    OfxImageClipHandle src, dst;
    std::map<std::string, OfxParamHandle> handles;
    std::recursive_mutex uiMutex;
    bool editing = false;
    std::mutex captureMutex;
    std::uint64_t captureGeneration = 0;
    bool captureRequested = false;
    double captureTime = 0;
    std::string pendingAction;
    Settings captureSettings;
    double captureX = .5, captureY = .5, captureRadius = .01;
    Capture capture;
    explicit Instance(OfxImageEffectHandle h) : handle(h) {
        check(effect->getParamSet(h, &parameters));
        check(effect->clipGetHandle(h, "Source", &src, nullptr));
        check(effect->clipGetHandle(h, "Output", &dst, nullptr));
    }
    OfxParamHandle p(const std::string &n) {
        auto it = handles.find(n);
        if (it != handles.end())
            return it->second;
        OfxParamHandle v = nullptr;
        check(param->paramGetHandle(parameters, n.c_str(), &v, nullptr));
        handles[n] = v;
        return v;
    }
    double number(const std::string &n, double time) {
        double v = 0;
        check(param->paramGetValueAtTime(p(n), time, &v));
        return v;
    }
    int integer(const std::string &n, double time) {
        int v = 0;
        check(param->paramGetValueAtTime(p(n), time, &v));
        return v;
    }
    void set(const std::string &n, double v) {
        check(param->paramSetValue(p(n), v));
    }
    void setInt(const std::string &n, int v) {
        check(param->paramSetValue(p(n), v));
    }
    std::string text(const std::string &n) {
        char *v = nullptr;
        check(param->paramGetValue(p(n), &v));
        return v ? v : "";
    }
    void setText(const std::string &n, const std::string &value) {
        check(param->paramSetValue(p(n), value.c_str()));
    }
    void status(const std::string &s) {
        std::string shortText = s;
        if (shortText.size() > 72) {
            std::size_t n = 69;
            while (n > 0 && (static_cast<unsigned char>(shortText[n]) & 0xc0) == 0x80)
                --n;
            shortText = shortText.substr(0, n) + "...";
        }
        check(param->paramSetValue(p("status"), shortText.c_str()));
        OfxParamHandle handle;
        OfxPropertySetHandle properties;
        if (param->paramGetHandle(parameters, "status", &handle, &properties) == kOfxStatOK)
            prop->propSetString(properties, kOfxParamPropHint, 0, s.c_str());
    }
    Settings settings(double time) {
        Settings s;
        for (auto &c : controls)
            s.*(c.member) = number(c.id, time);
        s.space = integer("space", time);
        s.premultiplied = integer("premult", time) != 0;
        return s;
    }
};
Instance &instance(OfxImageEffectHandle h) {
    auto *p = static_cast<Instance *>(getPointer(properties(h), kOfxPropInstanceData));
    if (!p)
        throw Failure{kOfxStatErrBadHandle};
    return *p;
}
struct Edit {
    Instance &i;
    bool old;
    Edit(Instance &v, const char *name) : i(v), old(v.editing) {
        i.editing = true;
        try {
            check(param->paramEditBegin(i.parameters, name));
        } catch (...) {
            i.editing = old;
            throw;
        }
    }
    ~Edit() {
        param->paramEditEnd(i.parameters);
        i.editing = old;
    }
};
struct ImageRef {
    OfxPropertySetHandle h = nullptr;
    Image image;
    ImageRef(OfxImageClipHandle clip, double time) {
        OfxStatus s = effect->clipGetImage(clip, time, nullptr, &h);
        if (s != kOfxStatOK) {
            h = nullptr;
            return;
        }
        try {
            int b[4];
            check(prop->propGetIntN(h, kOfxImagePropBounds, 4, b));
            image.bounds = {b[0], b[1], b[2], b[3]};
            image.data = getPointer(h, kOfxImagePropData);
            image.rowBytes = getInt(h, kOfxImagePropRowBytes);
            auto depth = getString(h, kOfxImageEffectPropPixelDepth);
            if (depth == kOfxBitDepthByte)
                image.depth = Depth::Byte;
            else if (depth == kOfxBitDepthShort)
                image.depth = Depth::Short;
            else if (depth == kOfxBitDepthHalf)
                image.depth = Depth::Half;
            else if (depth == kOfxBitDepthFloat)
                image.depth = Depth::Float;
            else
                throw Failure{kOfxStatErrImageFormat};
            auto comp = getString(h, kOfxImageEffectPropComponents);
            if (comp == kOfxImageComponentRGBA)
                image.components = 4;
            else if (comp == kOfxImageComponentRGB)
                image.components = 3;
            else
                throw Failure{kOfxStatErrImageFormat};
        } catch (...) {
            effect->clipReleaseImage(h);
            h = nullptr;
            throw;
        }
    }
    ~ImageRef() {
        if (h)
            effect->clipReleaseImage(h);
    }
};
#ifdef RENZU_METAL
ImageLayout layout(const Image &i) {
    if (i.rowBytes < std::numeric_limits<int>::min() ||
        i.rowBytes > std::numeric_limits<int>::max())
        throw Failure{kOfxStatErrImageFormat};
    return {i.bounds.x1,     i.bounds.y1,  i.bounds.x2, i.bounds.y2,
            int(i.rowBytes), int(i.depth), i.components};
}
std::string libraryPath() {
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void *>(&libraryPath), &info) || !info.dli_fname)
        throw Failure{kOfxStatErrMissingHostFeature};
    std::string p = info.dli_fname;
    return p.substr(0, p.find_last_of('/')) + "/../Resources/renzu_grade.metallib";
}
#endif
OfxPropertySetHandle define(OfxParamSetHandle set, const char *type, const char *id,
                            const char *label, const char *group = nullptr, bool hidden = false) {
    OfxPropertySetHandle p;
    check(param->paramDefine(set, type, id, &p));
    stringSet(p, kOfxPropLabel, label);
    if (group)
        stringSet(p, kOfxParamPropParent, group);
    if (std::strcmp(type, kOfxParamTypeGroup) && std::strcmp(type, kOfxParamTypePage) &&
        std::strcmp(type, kOfxParamTypePushButton)) {
        intSet(p, kOfxParamPropAnimates, 0);
        intSet(p, kOfxParamPropPersistant, 1);
    }
    if (hidden)
        intSet(p, kOfxParamPropSecret, 1);
    return p;
}
void numeric(OfxParamSetHandle set, const char *id, const char *label, const char *group,
             double value, double min, double max, bool hidden = false,
             const char *hint = nullptr) {
    auto p = define(set, kOfxParamTypeDouble, id, label, group, hidden);
    doubleSet(p, kOfxParamPropDefault, value);
    doubleSet(p, kOfxParamPropMin, min);
    doubleSet(p, kOfxParamPropMax, max);
    doubleSet(p, kOfxParamPropDisplayMin, min);
    doubleSet(p, kOfxParamPropDisplayMax, max);
    doubleSet(p, kOfxParamPropIncrement, .01);
    bool percent = std::strstr(id, "Amount") || std::strcmp(id, "vibrance") == 0 ||
                   std::strcmp(id, "wbTint") == 0 || std::strcmp(id, "matchStrength") == 0;
    if (percent || std::strcmp(id, "wbTemperature") == 0) {
        intSet(p, kOfxParamPropDigits, 0);
        doubleSet(p, kOfxParamPropIncrement, std::strcmp(id, "wbTemperature") == 0 ? 10 : 1);
    } else if (std::strcmp(id, "exposure") == 0)
        intSet(p, kOfxParamPropDigits, 2);
    if (hint)
        stringSet(p, kOfxParamPropHint, hint);
}
void button(OfxParamSetHandle s, const char *id, const char *label, const char *group) {
    define(s, kOfxParamTypePushButton, id, label, group);
}
void textControl(OfxParamSetHandle s, const char *id, const char *label, const char *group,
                 const char *value = "", bool hidden = false, bool readOnly = false) {
    auto p = define(s, kOfxParamTypeString, id, label, group, hidden);
    stringSet(p, kOfxParamPropDefault, value);
    stringSet(p, kOfxParamPropStringMode,
              readOnly ? kOfxParamStringIsLabel : kOfxParamStringIsSingleLine);
    if (readOnly)
        intSet(p, kOfxParamPropEnabled, 0);
    intSet(p, kOfxParamPropEvaluateOnChange, 0);
}
std::array<std::string, referenceSlots + 1> referenceLabels() {
    std::array<std::string, referenceSlots + 1> labels;
    labels[0] = "Latest saved reference";
    for (int slot = 1; slot <= referenceSlots; ++slot)
        labels[slot] = std::to_string(slot) + " - Empty";
    // Discovery must remain possible if the optional external library is missing
    // or inaccessible. Explicit user actions report storage errors in Status.
    try {
        ReferenceStore library(ReferenceStore::defaultDirectory());
        for (auto &r : library.list())
            labels[r.slot] = std::to_string(r.slot) + " - " + r.name;
    } catch (const std::exception &) {
    }
    return labels;
}
OfxStatus overlay(const char *action, const void *handle, OfxPropertySetHandle in,
                  OfxPropertySetHandle out);
OfxStatus describe(OfxImageEffectHandle h) {
    auto p = properties(h);
    stringSet(p, kOfxPropLabel, "Renzu Grade");
    stringSet(p, kOfxImageEffectPluginPropGrouping, "Color Grading");
    optionalStringSet(
        p, kOfxPropPluginDescription,
        "Renzu Grade for DaVinci Resolve. Independent open-source photographic grading.");
    stringSet(p, kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextFilter);
    stringSet(p, kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextGeneral, 1);
    const char *depths[] = {kOfxBitDepthFloat, kOfxBitDepthHalf, kOfxBitDepthShort,
                            kOfxBitDepthByte};
    for (int j = 0; j < 4; ++j)
        stringSet(p, kOfxImageEffectPropSupportedPixelDepths, depths[j], j);
    intSet(p, kOfxImageEffectPropSupportsTiles, 0);
    intSet(p, kOfxImageEffectPropSupportsMultiResolution, 1);
    intSet(p, kOfxImageEffectPropTemporalClipAccess, 0);
    intSet(p, kOfxImageEffectPropSupportsMultipleClipDepths, 0);
    intSet(p, kOfxImageEffectPluginPropHostFrameThreading, 0);
    stringSet(p, kOfxImageEffectPluginRenderThreadSafety, kOfxImageEffectRenderFullySafe);
#ifdef RENZU_METAL
    optionalStringSet(p, kOfxImageEffectPropMetalRenderSupported, "true");
    optionalStringSet(p, kOfxImageEffectPropMetalTextureSupported, "true");
#endif
    if (interact && getInt(host->host, kOfxImageEffectPropSupportsOverlays))
        check(prop->propSetPointer(p, kOfxImageEffectPluginPropOverlayInteractV1, 0,
                                   reinterpret_cast<void *>(&overlay)));
    return kOfxStatOK;
}
OfxStatus describeContext(OfxImageEffectHandle h) {
    for (const char *name : {"Source", "Output"}) {
        OfxPropertySetHandle p;
        check(effect->clipDefine(h, name, &p));
        stringSet(p, kOfxImageEffectPropSupportedComponents, kOfxImageComponentRGBA);
        stringSet(p, kOfxImageEffectPropSupportedComponents, kOfxImageComponentRGB, 1);
        intSet(p, kOfxImageEffectPropSupportsTiles, 0);
    }
    OfxParamSetHandle s;
    check(effect->getParamSet(h, &s));
    auto group = [&](const char *id, const char *label) {
        auto p = define(s, kOfxParamTypeGroup, id, label);
        optionalIntSet(p, kOfxParamPropGroupOpen, 1);
    };
    auto boolean = [&](const char *id, const char *label, bool value, const char *parent,
                       bool hidden = false) {
        auto p = define(s, kOfxParamTypeBoolean, id, label, parent, hidden);
        intSet(p, kOfxParamPropDefault, value);
    };
    auto integer = [&](const char *id, int value) {
        auto p = define(s, kOfxParamTypeInteger, id, id, nullptr, true);
        intSet(p, kOfxParamPropDefault, value);
    };
    // Declaration order is the hierarchical UI order in Resolve.
    button(s, "auto", "Auto", nullptr);
    textControl(s, "status", "Status", nullptr, "Ready", false, true);
    group("whiteBalance", "WB");
    button(s, "pick", "Pick WB", "whiteBalance");
    auto preset = define(s, kOfxParamTypeChoice, "wbPreset", "WB", "whiteBalance");
    intSet(preset, kOfxParamPropDefault, 0);
    const char *presets[] = {"As Shot", "Auto", "Custom"};
    for (int n = 0; n < 3; ++n)
        stringSet(preset, kOfxParamPropChoiceOption, presets[n], n);
    stringSet(preset, kOfxParamPropHint,
              "As Shot resets white balance. Auto analyzes this frame once. Editing Temp or Tint "
              "selects Custom.");
    numeric(s, "wbTemperature", "Temp (K)", "whiteBalance", 6504, 2000, 25000, false,
            "Correlated color temperature of the assumed illuminant, relative to neutral D65 at "
            "6504 K.");
    numeric(s, "wbTint", "Tint", "whiteBalance", 0, -250, 250);
    numeric(s, "exposure", "Exposure", nullptr, 0, -5, 5, false,
            "Scene-linear exposure compensation in stops (EV).");
    numeric(s, "contrastAmount", "Contrast", nullptr, 0, -100, 100);
    group("matchGroup", "Match / Reference");
    textControl(s, "referenceName", "Name", "matchGroup");
    auto reference = define(s, kOfxParamTypeChoice, "reference", "Reference", "matchGroup");
    intSet(reference, kOfxParamPropDefault, 0);
    auto labels = referenceLabels();
    for (int n = 0; n <= referenceSlots; ++n)
        stringSet(reference, kOfxParamPropChoiceOption, labels[n].c_str(), n);
    stringSet(reference, kOfxParamPropHint,
              "Latest creates a new reference. A numbered slot is replaced only when you save to "
              "that slot.");
    button(s, "saveReference", "Save Reference", "matchGroup");
    button(s, "refreshReferences", "Load Reference", "matchGroup");
    textControl(s, "referenceInfo", "Selected", "matchGroup", "No reference", false, true);
    boolean("matchWB", "White Balance", true, "matchGroup");
    boolean("matchExposure", "Exposure", true, "matchGroup");
    boolean("matchContrast", "Contrast", false, "matchGroup");
    boolean("matchColor", "Color (Vibrance + Saturation)", false, "matchGroup");
    numeric(s, "matchStrength", "Amount (%)", "matchGroup", 100, 0, 100);
    button(s, "match", "Match", "matchGroup");
    group("hdr", "HDR");
    numeric(s, "highlightAmount", "Highlights", "hdr", 0, -100, 100);
    numeric(s, "shadowAmount", "Shadows", "hdr", 0, -100, 100);
    numeric(s, "whiteAmount", "Whites", "hdr", 0, -100, 100);
    numeric(s, "blackAmount", "Blacks", "hdr", 0, -100, 100);
    numeric(s, "vibrance", "Vibrance", nullptr, 0, -100, 100);
    numeric(s, "saturationAmount", "Saturation", nullptr, 0, -100, 100);
    // Original identifiers remain persistent for existing projects and undo.
    for (auto &c : controls)
        if (std::strcmp(c.id, "exposure") && std::strcmp(c.id, "vibrance"))
            numeric(s, c.id, c.label, nullptr, c.value, c.min, c.max, true);
    for (auto id : {"autoTone", "autoWB", "sample", "capture", "applyTone", "applyWB", "applyPatch",
                    "reset", "saveCapturedReference", "matchCaptured"}) {
        auto p = define(s, kOfxParamTypePushButton, id, id, nullptr, true);
        (void)p;
    }
    numeric(s, "strength", "strength", nullptr, 100, 0, 100, true);
    numeric(s, "patchX", "patchX", nullptr, .5, 0, 1, true);
    numeric(s, "patchY", "patchY", nullptr, .5, 0, 1, true);
    numeric(s, "patchRadius", "patchRadius", nullptr, .01, .0001, .25, true);
    auto sp = define(s, kOfxParamTypeChoice, "space", "space", nullptr, true);
    intSet(sp, kOfxParamPropDefault, 0);
    const char *spaces[] = {"DaVinci Wide Gamut / Intermediate", "Rec.709 / Gamma 2.4",
                            "Scene Linear / Rec.709 primaries"};
    for (int j = 0; j < 3; ++j)
        stringSet(sp, kOfxParamPropChoiceOption, spaces[j], j);
    boolean("premult", "premult", false, nullptr, true);
    boolean("autoValid", "autoValid", false, nullptr, true);
    boolean("matchValid", "matchValid", false, nullptr, true);
    boolean("pickerArmed", "pickerArmed", false, nullptr, true);
    numeric(s, "appliedStrength", "appliedStrength", nullptr, 100, 0, 100, true);
    numeric(s, "appliedMatchStrength", "appliedMatchStrength", nullptr, 100, 0, 100, true);
    integer("captureRevision", 0);
    integer("referenceSnapshotSlot", 0);
    integer("matchedMode", 0);
    auto mode = define(s, kOfxParamTypeChoice, "matchMode", "matchMode", nullptr, true);
    intSet(mode, kOfxParamPropDefault, 0);
    for (int j = 0; j < 3; ++j)
        stringSet(mode, kOfxParamPropChoiceOption,
                  j == 0   ? "WB + Exposure"
                  : j == 1 ? "Exposure only"
                           : "WB only",
                  j);
    textControl(s, "referenceSnapshot", "referenceSnapshot", nullptr, "", true);
    textControl(s, "matchReferenceId", "matchReferenceId", nullptr, "", true);
    for (auto n : {"temperature", "tint", "exposure", "contrast", "vibrance", "saturation"})
        for (auto prefix : {"matchBase_", "matchTarget_"}) {
            auto id = std::string(prefix) + n;
            double value =
                std::strcmp(n, "contrast") == 0 || std::strcmp(n, "saturation") == 0 ? 1 : 0;
            numeric(s, id.c_str(), id.c_str(), nullptr, value, -1000, 1000, true);
        }
    for (int j = 0; j < 6; ++j)
        for (auto prefix : {"base_", "target_"}) {
            auto id = std::string(prefix) + toneNames[j];
            numeric(s, id.c_str(), id.c_str(), nullptr, j == 5 ? 1 : 0, -1000, 1000, true);
        }
    auto page = define(s, kOfxParamTypePage, "controls", "Controls");
    int child = 0;
    for (auto n : {"auto",
                   "status",
                   "pick",
                   "wbPreset",
                   "wbTemperature",
                   "wbTint",
                   "exposure",
                   "contrastAmount",
                   "referenceName",
                   "reference",
                   "saveReference",
                   "refreshReferences",
                   "referenceInfo",
                   "matchWB",
                   "matchExposure",
                   "matchContrast",
                   "matchColor",
                   "matchStrength",
                   "match",
                   "highlightAmount",
                   "shadowAmount",
                   "whiteAmount",
                   "blackAmount",
                   "vibrance",
                   "saturationAmount"})
        stringSet(page, kOfxParamPropPageChild, n, child++);
    return kOfxStatOK;
}
void blendStrength(Instance &i, double time, bool preserveManual = true) {
    if (!i.integer("autoValid", time))
        return;
    double a = i.number("strength", time) / 100.;
    double previous = i.number("appliedStrength", time) / 100.;
    for (int j = 0; j < 6; ++j) {
        auto n = toneNames[j];
        double b = i.number(std::string("base_") + n, time),
               t = i.number(std::string("target_") + n, time);
        if (preserveManual) {
            double delta = i.number(n, time) - (b + (t - b) * previous);
            b += delta;
            t += delta;
            i.set(std::string("base_") + n, b);
            i.set(std::string("target_") + n, t);
        }
        i.set(n, std::clamp(b + (t - b) * a, controls[j + 2].min, controls[j + 2].max));
    }
    i.set("appliedStrength", a * 100.);
}
void syncUI(Instance &i, double time) {
    auto settings = i.settings(time);
    auto wb = whiteBalanceToKelvin(settings.temperature, settings.tint);
    const std::pair<const char *, double> values[] = {
        {"wbTemperature", wb.kelvin},
        {"wbTint", std::clamp(wb.tint, -250., 250.)},
        {"contrastAmount", (settings.contrast - 1) * 100},
        {"saturationAmount", (settings.saturation - 1) * 100},
        {"highlightAmount", settings.lights * 100 / 3},
        {"shadowAmount", settings.midtones * 100 / 3},
        {"whiteAmount", settings.highlights * 100 / 3},
        {"blackAmount", settings.shadows * 100 / 3}};
    for (auto &v : values)
        if (std::abs(i.number(v.first, time) - v.second) > 1.e-6)
            i.set(v.first, v.second);
}
void applyTone(Instance &i, const Capture &c, double time) {
    i.setInt("matchValid", 0);
    Settings base = i.settings(time);
    bool unchanged = i.integer("autoValid", time) != 0;
    double a = i.number("appliedStrength", time) / 100.;
    for (auto n : toneNames) {
        double b = i.number(std::string("base_") + n, time),
               t = i.number(std::string("target_") + n, time);
        unchanged &= std::abs(i.number(n, time) - (b + (t - b) * a)) < 1.e-7;
    }
    if (unchanged)
        for (int j = 0; j < 6; ++j)
            base.*controls[j + 2].member = i.number(std::string("base_") + toneNames[j], time);
    Settings target = autoTone(analyze(c.pixels, c.settings.space), base, 1);
    for (int j = 0; j < 6; ++j) {
        i.set(std::string("base_") + toneNames[j], base.*controls[j + 2].member);
        i.set(std::string("target_") + toneNames[j], target.*controls[j + 2].member);
    }
    i.setInt("autoValid", 1);
    blendStrength(i, time, false);
    syncUI(i, time);
    i.status("Auto Tone applied from frame " + std::to_string(c.time) + "; sliders remain fixed.");
}
void applyWB(Instance &i, const WBResult &r) {
    if (r.reliable) {
        i.setInt("matchValid", 0);
        i.set("temperature", r.temperature);
        i.set("tint", r.tint);
    }
    i.status(r.status);
}
Capture captureCPU(const Image &image, double time, const Settings &s, double x, double y,
                   double radius, bool picker = false) {
    Capture c;
    c.time = time;
    c.settings = s;
    c.pixels = sampleImage(image, s.space, s.premultiplied);
    c.patch = sampleNeutral(image, s.space, s.premultiplied, x, y, radius);
    c.ready = !c.pixels.empty();
    if (picker && valid(image)) {
        int width = image.bounds.x2 - image.bounds.x1, height = image.bounds.y2 - image.bounds.y1;
        int step = std::max(1, (std::max(width, height) + 1023) / 1024);
        c.pickerWidth = (width + step - 1) / step;
        c.pickerHeight = (height + step - 1) / step;
        c.picker.resize(std::size_t(c.pickerWidth) * c.pickerHeight * 4);
        for (int py = 0; py < c.pickerHeight; ++py)
            for (int px = 0; px < c.pickerWidth; ++px) {
                auto pixel =
                    readPixel(image, image.bounds.x1 + std::min(width - 1, px * step + step / 2),
                              image.bounds.y1 + std::min(height - 1, py * step + step / 2));
                auto offset = (std::size_t(py) * c.pickerWidth + px) * 4;
                for (int k = 0; k < 4; ++k)
                    c.picker[offset + k] = pixel[k];
            }
    }
    return c;
}
Capture directCapture(Instance &i, double time) {
    ImageRef src(i.src, time);
    if (!src.h || !valid(src.image))
        return {};
    return captureCPU(src.image, time, i.settings(time), i.number("patchX", time),
                      i.number("patchY", time), i.number("patchRadius", time),
                      i.integer("pickerArmed", time) != 0);
}
void refreshReferenceLabels(Instance &i) {
    OfxParamHandle handle;
    OfxPropertySetHandle p;
    check(param->paramGetHandle(i.parameters, "reference", &handle, &p));
    auto labels = referenceLabels();
    for (int slot = 0; slot <= referenceSlots; ++slot) {
        // Slot indices never change, even if a host does not refresh option text.
        auto status = prop->propSetString(p, kOfxParamPropChoiceOption, slot, labels[slot].c_str());
        if (status != kOfxStatErrUnknown && status != kOfxStatErrUnsupported)
            check(status);
    }
}
void bindReference(Instance &i, const ReferenceFrame &reference, int selection) {
    i.setText("referenceSnapshot", serializeReference(reference));
    i.setInt("referenceSnapshotSlot", selection);
    i.setText("referenceInfo", reference.name);
}
ReferenceFrame selectedReference(Instance &i, double time, bool reload = false) {
    int slot = i.integer("reference", time);
    auto snapshot = i.text("referenceSnapshot");
    if (!reload && !snapshot.empty() && i.integer("referenceSnapshotSlot", time) == slot)
        return deserializeReference(snapshot);
    ReferenceStore library(ReferenceStore::defaultDirectory());
    auto reference = library.load(slot);
    bindReference(i, reference, slot);
    return reference;
}
void saveReference(Instance &i, const Capture &capture, double time) {
    auto reference =
        makeReference(capture.pixels, capture.settings, i.text("referenceName"), capture.time);
    ReferenceStore library(ReferenceStore::defaultDirectory());
    int selection = i.integer("reference", time);
    reference = library.save(std::move(reference), selection);
    // Keep Latest selected after allocating a slot, so subsequent Saves create
    // new named references rather than unexpectedly replacing the first one.
    bindReference(i, reference, selection);
    refreshReferenceLabels(i);
    i.status("Saved: " + reference.name);
}
constexpr const char *matchNames[] = {"temperature", "tint",     "exposure",
                                      "contrast",    "vibrance", "saturation"};
double Settings::*const matchMembers[] = {&Settings::temperature, &Settings::tint,
                                          &Settings::exposure,    &Settings::contrast,
                                          &Settings::vibrance,    &Settings::saturation};
void blendMatchStrength(Instance &i, double time, bool preserveManual = true) {
    if (!i.integer("matchValid", time))
        return;
    double strength = i.number("matchStrength", time) / 100.;
    if (!std::isfinite(strength)) {
        i.status("Match Strength must be a finite percentage");
        return;
    }
    strength = std::clamp(strength, 0., 1.);
    double previous = i.number("appliedMatchStrength", time) / 100.;
    for (int j = 0; j < 6; ++j) {
        auto name = matchNames[j];
        double base = i.number(std::string("matchBase_") + name, time);
        double target = i.number(std::string("matchTarget_") + name, time);
        if (preserveManual) {
            double offset = i.number(name, time) - (base + (target - base) * previous);
            base += offset;
            target += offset;
            i.set(std::string("matchBase_") + name, base);
            i.set(std::string("matchTarget_") + name, target);
        }
        const double limits[][2] = {{-600, 200}, {-500, 500}, {-5, 5}, {0, 2}, {-100, 100}, {0, 2}};
        i.set(name, std::clamp(base + (target - base) * strength, limits[j][0], limits[j][1]));
    }
    i.set("appliedMatchStrength", strength * 100.);
    syncUI(i, time);
}
void applyMatch(Instance &i, const Capture &capture, double time) {
    auto reference = selectedReference(i, time);
    Settings base = i.settings(time);
    MatchFields fields{i.integer("matchWB", time) != 0, i.integer("matchExposure", time) != 0,
                       i.integer("matchContrast", time) != 0, i.integer("matchColor", time) != 0};
    int mode = int(fields.whiteBalance) + 2 * int(fields.exposure) + 4 * int(fields.contrast) +
               8 * int(fields.color);
    bool unchanged = i.integer("matchValid", time) && i.text("matchReferenceId") == reference.id &&
                     i.integer("matchedMode", time) == mode;
    double strength = i.number("appliedMatchStrength", time) / 100.;
    for (auto name : matchNames) {
        double b = i.number(std::string("matchBase_") + name, time);
        double t = i.number(std::string("matchTarget_") + name, time);
        unchanged &= std::abs(i.number(name, time) - (b + (t - b) * strength)) < 1.e-7;
    }
    if (unchanged)
        for (int j = 0; j < 6; ++j)
            base.*matchMembers[j] = i.number(std::string("matchBase_") + matchNames[j], time);
    auto result = matchReference(capture.pixels, base, reference, fields);
    if (result.applied()) {
        for (int j = 0; j < 6; ++j) {
            i.set(std::string("matchBase_") + matchNames[j], base.*matchMembers[j]);
            i.set(std::string("matchTarget_") + matchNames[j], result.settings.*matchMembers[j]);
        }
        i.setText("matchReferenceId", reference.id);
        i.setInt("matchedMode", mode);
        i.setInt("matchValid", 1);
        if (result.exposureMatched)
            i.setInt("autoValid", 0);
        blendMatchStrength(i, time, false);
    }
    i.status(result.status);
}
bool acquireFrame(Instance &i, double time, const std::string &action, Capture &out,
                  OfxPropertySetHandle args) {
    if (!getInt(args, kOfxImageEffectPropMetalEnabled) &&
        !getInt(args, kOfxImageEffectPropMetalTextureEnabled))
        out = directCapture(i, time);
    if (out.ready) {
        std::lock_guard<std::mutex> lock(i.captureMutex);
        ++i.captureGeneration;
        i.captureRequested = false;
        i.pendingAction.clear();
        i.capture = {};
        return true;
    }
    bool cached = false;
    {
        std::lock_guard<std::mutex> lock(i.captureMutex);
        if (i.pendingAction == action && std::abs(i.captureTime - time) < 1.e-6 &&
            i.capture.ready) {
            out = std::move(i.capture);
            i.capture = {};
            i.pendingAction.clear();
            cached = true;
        }
    }
    Settings settings = i.settings(time);
    if (cached && out.settings.space == settings.space &&
        out.settings.premultiplied == settings.premultiplied) {
        out.settings = settings;
        return true;
    }
    double x = i.number("patchX", time), y = i.number("patchY", time),
           radius = i.number("patchRadius", time);
    {
        std::lock_guard<std::mutex> lock(i.captureMutex);
        ++i.captureGeneration;
        i.capture = {};
        i.captureRequested = true;
        i.captureTime = time;
        i.captureSettings = settings;
        i.captureX = x;
        i.captureY = y;
        i.captureRadius = radius;
        i.pendingAction = action;
    }
    int revision = i.integer("captureRevision", time);
    i.setInt("captureRevision", revision == 2147483647 ? 0 : revision + 1);
    i.status(action == "pick" ? "Preparing picker; click a neutral area after the viewer updates"
                              : "Preparing frame; press the same button again");
    return false;
}
OfxStatus changedLegacy(OfxImageEffectHandle h, OfxPropertySetHandle args);
OfxStatus changed(OfxImageEffectHandle h, OfxPropertySetHandle args) {
    auto &i = instance(h);
    std::lock_guard<std::recursive_mutex> lock(i.uiMutex);
    if (i.editing)
        return kOfxStatOK;
    auto name = getString(args, kOfxPropName);
    double time = getDouble(args, kOfxPropTime);
    if (getString(args, kOfxPropType) == kOfxTypeClip) {
        Edit edit(i, "Update controls");
        syncUI(i, time);
        if (i.integer("wbPreset", time) == 0 && (std::abs(i.number("temperature", time)) > 1.e-8 ||
                                                 std::abs(i.number("tint", time)) > 1.e-8))
            i.setInt("wbPreset", 2);
        {
            std::lock_guard<std::mutex> guard(i.captureMutex);
            i.capture = {};
            i.captureRequested = false;
            i.pendingAction.clear();
            ++i.captureGeneration;
        }
        return kOfxStatOK;
    }
    if (getString(args, kOfxPropChangeReason) != kOfxChangeUserEdited)
        return kOfxStatReplyDefault;
    bool alias = name == "wbTemperature" || name == "wbTint" || name == "contrastAmount" ||
                 name == "saturationAmount" || name == "highlightAmount" ||
                 name == "shadowAmount" || name == "whiteAmount" || name == "blackAmount";
    if (alias) {
        Edit edit(i, "Adjust grade");
        if (name == "wbTemperature" || name == "wbTint") {
            auto wb =
                whiteBalanceFromKelvin(i.number("wbTemperature", time), i.number("wbTint", time));
            i.set("temperature", wb[0]);
            i.set("tint", wb[1]);
            i.setInt("wbPreset", 2);
        } else if (name == "contrastAmount")
            i.set("contrast", 1 + i.number(name, time) / 100.);
        else if (name == "saturationAmount")
            i.set("saturation", 1 + i.number(name, time) / 100.);
        else {
            const char *target = name == "highlightAmount" ? "lights"
                                 : name == "shadowAmount"  ? "midtones"
                                 : name == "whiteAmount"   ? "highlights"
                                                           : "shadows";
            i.set(target, i.number(name, time) * 3 / 100.);
        }
        {
            std::lock_guard<std::mutex> guard(i.captureMutex);
            i.pendingAction.clear();
            i.captureRequested = false;
        }
        return kOfxStatOK;
    }
    if (name == "auto" || name == "wbPreset" || name == "pick" || name == "match" ||
        name == "saveReference") {
        Edit edit(i, name == "match" ? "Match" : "Renzu Grade");
        if (name == "wbPreset" && i.integer("wbPreset", time) != 1) {
            int preset = i.integer("wbPreset", time);
            if (preset == 2)
                return kOfxStatOK;
            i.set("temperature", 0);
            i.set("tint", 0);
            i.setInt("matchValid", 0);
            syncUI(i, time);
            i.status("WB preset applied");
            return kOfxStatOK;
        }
        if (name == "pick")
            i.setInt("pickerArmed", 1);
        else
            i.setInt("pickerArmed", 0);
        Capture capture;
        std::string action = name == "wbPreset" ? "autoWB" : name;
        if (!acquireFrame(i, time, action, capture, args)) {
            if (name == "wbPreset") {
                i.setInt("wbPreset", 2);
                i.status("Preparing frame; select WB Auto again");
            }
            return kOfxStatOK;
        }
        if (name == "pick") {
            std::lock_guard<std::mutex> guard(i.captureMutex);
            i.capture = std::move(capture);
            i.captureTime = time;
            i.pendingAction = "pick";
            i.status("Click a neutral gray or white area");
            return kOfxStatOK;
        }
        if (name == "auto") {
            auto result = autoGrade(capture.pixels, i.settings(time));
            if (result.applied) {
                for (auto &c : controls)
                    i.set(c.id, result.settings.*c.member);
                i.setInt("autoValid", 0);
                i.setInt("matchValid", 0);
                if (result.whiteBalanceApplied)
                    i.setInt("wbPreset", 1);
                syncUI(i, time);
            }
            i.status(result.status);
        } else if (name == "wbPreset") {
            applyWB(i, autoWB(capture.pixels, capture.settings.space));
            syncUI(i, time);
            i.setInt("wbPreset", 1);
        } else {
            try {
                if (name == "saveReference")
                    saveReference(i, capture, time);
                else {
                    auto before = i.settings(time);
                    applyMatch(i, capture, time);
                    if (std::abs(before.temperature - i.number("temperature", time)) +
                            std::abs(before.tint - i.number("tint", time)) >
                        1.e-8)
                        i.setInt("wbPreset", 2);
                }
            } catch (const std::exception &e) {
                i.status(std::string("Reference: ") + e.what());
            }
        }
        return kOfxStatOK;
    }
    return changedLegacy(h, args);
}
OfxStatus changedLegacy(OfxImageEffectHandle h, OfxPropertySetHandle args) {
    Instance &i = instance(h);
    std::lock_guard<std::recursive_mutex> lock(i.uiMutex);
    if (i.editing)
        return kOfxStatOK;
    if (getString(args, kOfxPropChangeReason) != kOfxChangeUserEdited)
        return kOfxStatReplyDefault;
    auto name = getString(args, kOfxPropName);
    double time = getDouble(args, kOfxPropTime);
    // Manual sliders need no private-state mutation. Detect offsets when the user
    // next invokes strength/auto; this also keeps host undo/redo restoration intact.
    if (name == "strength") {
        Edit e(i, "Auto Strength");
        blendStrength(i, time);
        syncUI(i, time);
        return kOfxStatOK;
    }
    if (name == "matchStrength") {
        Edit edit(i, "Match Strength");
        blendMatchStrength(i, time);
        return kOfxStatOK;
    }
    if (name == "reference" || name == "refreshReferences") {
        Edit edit(i, "Select reference");
        try {
            refreshReferenceLabels(i);
            auto reference = selectedReference(i, time, true);
            i.status("Loaded reference " + reference.name + ". Press Match to change the grade.");
        } catch (const std::exception &e) {
            i.setText("referenceSnapshot", "");
            i.setText("referenceInfo", "Reference unavailable. Save one or select another slot.");
            i.status(std::string("Reference unavailable: ") + e.what());
        }
        return kOfxStatOK;
    }
    if (name == "pick") {
        Edit e(i, "Pick Neutral Area");
        i.setInt("pickerArmed", 1);
        i.status("Click a neutral area with OpenFX overlay enabled, then Sample Neutral Patch. Or "
                 "set X/Y.");
        return kOfxStatOK;
    }
    if (name == "reset") {
        Edit e(i, "Reset Grade");
        for (auto &c : controls)
            i.set(c.id, c.value);
        i.set("strength", 100);
        i.setInt("autoValid", 0);
        i.setInt("matchValid", 0);
        i.set("matchStrength", 100);
        i.setInt("pickerArmed", 0);
        i.status("Grade reset");
        syncUI(i, time);
        return kOfxStatOK;
    }
    bool direct = name == "autoTone" || name == "autoWB" || name == "sample" || name == "capture" ||
                  name == "saveReference" || name == "match";
    bool saved = name == "applyTone" || name == "applyWB" || name == "applyPatch" ||
                 name == "saveCapturedReference" || name == "matchCaptured";
    if (!direct && !saved)
        return kOfxStatReplyDefault;
    Capture c;
    if (direct) {
        // Never dereference a negotiated GPU object as CPU memory in a callback.
        if (!getInt(args, kOfxImageEffectPropMetalEnabled) &&
            !getInt(args, kOfxImageEffectPropMetalTextureEnabled))
            c = directCapture(i, time);
    } else {
        std::lock_guard<std::mutex> guard(i.captureMutex);
        c = i.capture;
    }
    Edit edit(i, direct ? "Renzu automatic correction" : "Apply captured correction");
    if (name == "capture") {
        Settings requestedSettings = i.settings(time);
        double x = i.number("patchX", time), y = i.number("patchY", time),
               radius = i.number("patchRadius", time);
        {
            std::lock_guard<std::mutex> guard(i.captureMutex);
            ++i.captureGeneration;
            i.capture = c;
            i.captureRequested = !c.ready;
            i.captureTime = time;
            i.captureSettings = requestedSettings;
            i.captureX = x;
            i.captureY = y;
            i.captureRadius = radius;
        }
        // Parameter writes may invalidate host renders. Never hold captureMutex here.
        if (c.ready) {
            i.status("Frame captured. Use Apply Captured, Save Captured as Reference, or Match "
                     "Captured Frame.");
        } else {
            int revision = i.integer("captureRevision", time);
            i.setInt("captureRevision", revision == 2147483647 ? 0 : revision + 1);
            i.status("Capture armed for this frame. Let viewer render, then Apply Captured "
                     "Tone/WB/Patch.");
        }
        return kOfxStatOK;
    }
    if (!c.ready) {
        i.status(
            "Frame unavailable. Use Capture Current Frame, let viewer render, then Apply Captured, "
            "Save Captured as Reference, or Match Captured Frame.");
        return kOfxStatOK;
    }
    if (c.settings.space != i.integer("space", time) ||
        c.settings.premultiplied != (i.integer("premult", time) != 0)) {
        i.status("Encoding changed since capture. Capture the frame again.");
        return kOfxStatOK;
    }
    if (name == "saveReference" || name == "saveCapturedReference" || name == "match" ||
        name == "matchCaptured") {
        try {
            if (name == "saveReference" || name == "saveCapturedReference")
                saveReference(i, c, time);
            else
                applyMatch(i, c, time);
        } catch (const std::exception &e) {
            i.status(std::string("Reference / Match: ") + e.what());
        }
        return kOfxStatOK;
    }
    if (name == "autoTone" || name == "applyTone")
        applyTone(i, c, time);
    else if (name == "autoWB" || name == "applyWB")
        applyWB(i, autoWB(c.pixels, c.settings.space));
    else
        applyWB(i, c.patch);
    syncUI(i, time);
    return kOfxStatOK;
}
struct RenderJob {
    const Image &src;
    const Image &dst;
    Rect window;
    const KernelParams &params;
    OfxImageEffectHandle handle;
    std::atomic<bool> failed{false};
};
void cpuThread(unsigned index, unsigned count, void *arg) {
    auto &j = *static_cast<RenderJob *>(arg);
    Rect r = j.window;
    r.y1 = j.window.y1 + (j.window.y2 - j.window.y1) * index / count;
    r.y2 = j.window.y1 + (j.window.y2 - j.window.y1) * (index + 1) / count;
    try {
        renderRows(j.src, j.dst, r, j.params, [&] { return effect->abort(j.handle) != 0; });
    } catch (...) {
        j.failed = true;
    }
}
OfxStatus render(OfxImageEffectHandle h, OfxPropertySetHandle args) {
    Instance &i = instance(h);
    double time = getDouble(args, kOfxPropTime);
    ImageRef src(i.src, time), dst(i.dst, time);
    if (!dst.h)
        throw Failure{kOfxStatFailed};
    int w[4];
    check(prop->propGetIntN(args, kOfxImageEffectPropRenderWindow, 4, w));
    Rect window{std::max(w[0], dst.image.bounds.x1), std::max(w[1], dst.image.bounds.y1),
                std::min(w[2], dst.image.bounds.x2), std::min(w[3], dst.image.bounds.y2)};
    Settings settings = i.settings(time);
    auto p = prepare(settings);
    bool textures = getInt(args, kOfxImageEffectPropMetalTextureEnabled) != 0;
    bool gpu = textures || getInt(args, kOfxImageEffectPropMetalEnabled) != 0;
    bool doCapture = false;
    bool pickerCapture = false;
    std::uint64_t generation = 0;
    Settings cs;
    double cx = 0, cy = 0, cr = 0;
    {
        std::lock_guard<std::mutex> guard(i.captureMutex);
        if (i.captureRequested && std::abs(time - i.captureTime) < 1.e-6) {
            i.captureRequested = false;
            doCapture = true;
            generation = i.captureGeneration;
            cs = i.captureSettings;
            cx = i.captureX;
            cy = i.captureY;
            cr = i.captureRadius;
            pickerCapture = i.pendingAction == "pick";
        }
    }
    if (doCapture && src.h) {
        Capture c;
#ifdef RENZU_METAL
        if (gpu) {
            std::vector<unsigned char> bytes;
            std::string error;
            auto l = layout(src.image);
            if (downloadMetal(getPointer(args, kOfxImageEffectPropMetalCommandQueue),
                              src.image.data, l, textures, bytes, error)) {
                Image image = src.image;
                if (textures)
                    image.rowBytes = (l.x2 - l.x1) * l.components * bytesPerChannel(Depth(l.depth));
                image.data = bytes.data();
                if (image.rowBytes < 0)
                    image.data = bytes.data() + (l.y2 - l.y1 - 1) * (-image.rowBytes);
                c = captureCPU(image, time, cs, cx, cy, cr, pickerCapture);
            }
        } else
#endif
            c = captureCPU(src.image, time, cs, cx, cy, cr, pickerCapture);
        std::lock_guard<std::mutex> guard(i.captureMutex);
        if (generation == i.captureGeneration)
            i.capture = std::move(c);
    }
#ifdef RENZU_METAL
    if (gpu) {
        if (!src.h)
            return kOfxStatGPURenderFailed;
        Dispatch d{layout(src.image), layout(dst.image), window.x1,
                   window.y1,         window.x2,         window.y2};
        std::string error;
        if (!renderMetal(getPointer(args, kOfxImageEffectPropMetalCommandQueue), src.image.data,
                         dst.image.data, d, p, textures, libraryPath(), error))
            return kOfxStatGPURenderFailed;
        return kOfxStatOK;
    }
#else
    if (gpu)
        return kOfxStatErrUnsupported;
#endif
    if (!valid(dst.image) || (src.h && !valid(src.image)))
        return kOfxStatErrImageFormat;
    if (threading && window.y2 > window.y1) {
        unsigned cpus = 1;
        threading->multiThreadNumCPUs(&cpus);
        RenderJob j{src.image, dst.image, window, p, h};
        check(threading->multiThread(
            cpuThread, std::min(cpus, unsigned(std::max(1, (window.y2 - window.y1) / 16))), &j));
        if (j.failed)
            return kOfxStatFailed;
    } else
        renderRows(src.image, dst.image, window, p, [&] { return effect->abort(h) != 0; });
    return kOfxStatOK;
}
OfxStatus overlay(const char *action, const void *handle, OfxPropertySetHandle args,
                  OfxPropertySetHandle) {
    try {
        if (!std::strcmp(action, kOfxActionDescribe) ||
            !std::strcmp(action, kOfxActionCreateInstance) ||
            !std::strcmp(action, kOfxActionDestroyInstance))
            return kOfxStatOK;
        if (std::strcmp(action, kOfxInteractActionPenDown))
            return kOfxStatReplyDefault;
        OfxPropertySetHandle ip;
        check(interact->interactGetPropertySet((OfxInteractHandle)handle, &ip));
        auto h = (OfxImageEffectHandle)getPointer(ip, kOfxPropEffectInstance);
        auto &i = instance(h);
        std::lock_guard<std::recursive_mutex> guard(i.uiMutex);
        double time = getDouble(args, kOfxPropTime);
        if (!i.integer("pickerArmed", time))
            return kOfxStatReplyDefault;
        double point[2];
        check(prop->propGetDoubleN(args, kOfxInteractPropPenPosition, 2, point));
        OfxRectD rod;
        check(effect->clipGetRegionOfDefinition(i.src, time, &rod));
        if (rod.x2 <= rod.x1 || rod.y2 <= rod.y1)
            return kOfxStatReplyDefault;
        Edit edit(i, "Select neutral patch");
        double nx = std::clamp((point[0] - rod.x1) / (rod.x2 - rod.x1), 0., 1.);
        double ny = std::clamp((point[1] - rod.y1) / (rod.y2 - rod.y1), 0., 1.);
        Capture captured;
        {
            std::lock_guard<std::mutex> lock(i.captureMutex);
            captured = std::move(i.capture);
            i.capture = {};
            i.pendingAction.clear();
        }
        if (captured.picker.empty() || std::abs(captured.time - time) > 1.e-6) {
            i.status("Frame changed; press Pick WB again");
            i.setInt("pickerArmed", 0);
            return kOfxStatOK;
        }
        Image image{captured.picker.data(),
                    {0, 0, captured.pickerWidth, captured.pickerHeight},
                    captured.pickerWidth * 16,
                    Depth::Float,
                    4};
        auto wb = sampleNeutral(image, captured.settings.space, captured.settings.premultiplied, nx,
                                ny, .01);
        applyWB(i, wb);
        syncUI(i, time);
        if (wb.reliable)
            i.setInt("wbPreset", 2);
        i.set("patchX", nx);
        i.set("patchY", ny);
        i.setInt("pickerArmed", 0);
        i.status(wb.reliable ? "White balance sampled"
                             : "Choose a brighter, unclipped neutral area");
        return kOfxStatOK;
    } catch (const Failure &e) {
        return e.status;
    } catch (...) {
        return kOfxStatFailed;
    }
}
OfxStatus entry(const char *action, const void *handle, OfxPropertySetHandle in,
                OfxPropertySetHandle out) {
    try {
        auto h = (OfxImageEffectHandle)handle;
        if (!std::strcmp(action, kOfxActionLoad)) {
            if (!host)
                return kOfxStatErrMissingHostFeature;
            prop = (OfxPropertySuiteV1 *)host->fetchSuite(host->host, kOfxPropertySuite, 1);
            effect = (OfxImageEffectSuiteV1 *)host->fetchSuite(host->host, kOfxImageEffectSuite, 1);
            param = (OfxParameterSuiteV1 *)host->fetchSuite(host->host, kOfxParameterSuite, 1);
            threading =
                (OfxMultiThreadSuiteV1 *)host->fetchSuite(host->host, kOfxMultiThreadSuite, 1);
            interact = (OfxInteractSuiteV1 *)host->fetchSuite(host->host, kOfxInteractSuite, 1);
            return prop && effect && param ? kOfxStatOK : kOfxStatErrMissingHostFeature;
        }
        if (!std::strcmp(action, kOfxActionDescribe))
            return describe(h);
        if (!std::strcmp(action, kOfxImageEffectActionDescribeInContext))
            return describeContext(h);
        if (!std::strcmp(action, kOfxActionCreateInstance)) {
            auto i =
                std::make_unique<Instance>(h); // Resolve all handles before concurrent rendering.
            for (auto &c : controls)
                i->p(c.id);
            for (auto n : {"space",
                           "premult",
                           "status",
                           "strength",
                           "appliedStrength",
                           "autoValid",
                           "pickerArmed",
                           "captureRevision",
                           "patchX",
                           "patchY",
                           "patchRadius",
                           "reference",
                           "referenceName",
                           "referenceInfo",
                           "referenceSnapshot",
                           "referenceSnapshotSlot",
                           "matchMode",
                           "matchStrength",
                           "matchValid",
                           "appliedMatchStrength",
                           "matchReferenceId",
                           "matchedMode"})
                i->p(n);
            for (auto n :
                 {"wbPreset", "wbTemperature", "wbTint", "contrastAmount", "saturationAmount",
                  "highlightAmount", "shadowAmount", "whiteAmount", "blackAmount", "matchWB",
                  "matchExposure", "matchContrast", "matchColor"})
                i->p(n);
            for (auto n : matchNames) {
                i->p(std::string("matchBase_") + n);
                i->p(std::string("matchTarget_") + n);
            }
            for (auto n : toneNames) {
                i->p(std::string("base_") + n);
                i->p(std::string("target_") + n);
            }
            check(prop->propSetPointer(properties(h), kOfxPropInstanceData, 0, i.get()));
            i.release();
            return kOfxStatOK;
        }
        if (!std::strcmp(action, kOfxActionDestroyInstance)) {
            delete &instance(h);
            prop->propSetPointer(properties(h), kOfxPropInstanceData, 0, nullptr);
            return kOfxStatOK;
        }
        if (!std::strcmp(action, kOfxActionInstanceChanged))
            return changed(h, in);
        if (!std::strcmp(action, kOfxImageEffectActionRender))
            return render(h, in);
        if (!std::strcmp(action, kOfxImageEffectActionGetClipPreferences)) {
            OfxImageClipHandle clip;
            OfxPropertySetHandle sourceProperties;
            check(effect->clipGetHandle(h, "Source", &clip, &sourceProperties));
            auto premult = getString(sourceProperties, kOfxImageEffectPropPreMultiplication,
                                     kOfxImageUnPreMultiplied);
            stringSet(out, kOfxImageEffectPropPreMultiplication, premult.c_str());
            return kOfxStatOK;
        }
        if (!std::strcmp(action, kOfxImageEffectActionGetRegionOfDefinition)) {
            OfxRectD r;
            check(effect->clipGetRegionOfDefinition(instance(h).src, getDouble(in, kOfxPropTime),
                                                    &r));
            double a[] = {r.x1, r.y1, r.x2, r.y2};
            check(prop->propSetDoubleN(out, kOfxImageEffectPropRegionOfDefinition, 4, a));
            return kOfxStatOK;
        }
        // Identity is deliberately not short-circuited: capture fallback must receive a render.
        return kOfxStatReplyDefault;
    } catch (const Failure &e) {
        return e.status;
    } catch (const std::bad_alloc &) {
        return kOfxStatErrMemory;
    } catch (...) {
        return kOfxStatFailed;
    }
}
void setHost(OfxHost *h) {
    host = h;
}
OfxPlugin plugin = {kOfxImageEffectPluginApi, 1, "org.renzu.grade", 0, 3, setHost, entry};
} // namespace
#ifdef _WIN32
#define RENZU_EXPORT __declspec(dllexport)
#else
#define RENZU_EXPORT __attribute__((visibility("default")))
#endif
extern "C" RENZU_EXPORT int OfxGetNumberOfPlugins() {
    return 1;
}
extern "C" RENZU_EXPORT OfxPlugin *OfxGetPlugin(int n) {
    return n == 0 ? &plugin : nullptr;
}
