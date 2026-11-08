// SPDX-License-Identifier: GPL-3.0-only
#include "Platform.h"
#include "renzu/Match.h"
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace renzu;
int checks = 0;
void require(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
void checkNear(double a, double b, double tolerance, const char *message) {
    require(std::abs(a - b) <= tolerance, message);
}
struct Scratch {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("renzu-match-test-" +
         std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    Scratch() {
        std::filesystem::create_directories(path);
    }
    ~Scratch() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
std::vector<V3> ramp() {
    std::vector<V3> out;
    for (int j = 0; j < 8192; ++j) {
        float y = .04f + .35f * j / 8191;
        out.push_back({y, y, y});
    }
    return out;
}
double chromaError(const std::vector<V3> &a, const std::vector<V3> &b) {
    double e = 0;
    for (std::size_t j = 0; j < a.size(); ++j) {
        auto x = a[j], y = b[j];
        double lx = luma(x, 2), ly = luma(y, 2);
        e += std::abs(x.r / lx - y.r / ly) + std::abs(x.b / lx - y.b / ly);
    }
    return e / a.size();
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
    try {
        if (argc == 3 && std::filesystem::path(argv[1]) == "--save-reference") {
            Settings s;
            s.space = 2;
            ReferenceStore(std::filesystem::path(argv[2]))
                .save(makeReference(ramp(), s, "Child process", 0));
            return 0;
        }
        auto input = ramp();
        Settings linear;
        linear.space = 2;
        for (int from = 0; from < 3; ++from)
            for (int to = 0; to < 3; ++to) {
                V3 c{-.1, .3, 1.2};
                auto roundtrip = convertLinear(convertLinear(c, from, to), to, from);
                checkNear(c.r, roundtrip.r, 2.e-6, "working primary roundtrip r");
                checkNear(c.g, roundtrip.g, 2.e-6, "working primary roundtrip g");
                checkNear(c.b, roundtrip.b, 2.e-6, "working primary roundtrip b");
            }
        Settings look = linear;
        look.temperature = 28;
        look.tint = -11;
        look.exposure = .65;
        auto reference = makeReference(input, look, "Камера A — общий", 42.5);
        require(reference.pixels.size() == maxReferenceSamples, "reference sample budget");
        auto expected = gradeSamples(input, look);
        checkNear(reference.pixels[300].r, expected[300].r, 0, "reference stores graded output");
        std::vector<V3> target = input;
        for (auto &c : target)
            c = mul(c, .5);
        auto match = matchReference(target, linear, reference, MatchMode::WBAndExposure);
        require(match.exposureMatched && match.whiteBalanceMatched, "WB and exposure fit applied");
        checkNear(match.settings.exposure, 1.65, .025, "reference relative exposure");
        checkNear(match.settings.temperature, 28, 1, "reference warmth preserved");
        checkNear(match.settings.tint, -11, 1, "reference tint preserved");
        auto rendered = gradeSamples(target, match.settings);
        auto resultStats = analyze(rendered, 2), refStats = analyze(reference.pixels, 2);
        checkNear(std::log2(resultStats.median / refStats.median), 0, .015, "matched median");
        require(chromaError(rendered, reference.pixels) < .004, "matched chromaticity");
        for (auto cast : {std::array<double, 3>{-24, 12, -1}, {22, -18, .4}, {-10, -20, -.5}}) {
            Settings observed = linear;
            observed.temperature = cast[0];
            observed.tint = cast[1];
            observed.exposure = cast[2];
            auto camera = gradeSamples(input, observed);
            auto correction = matchReference(camera, linear, reference, MatchMode::WBAndExposure);
            require(correction.whiteBalanceMatched && correction.exposureMatched,
                    "cast camera matches WB and exposure");
            auto corrected = gradeSamples(camera, correction.settings);
            require(chromaError(corrected, reference.pixels) < .006,
                    "warm/cool/green/magenta camera cast corrected");
            checkNear(std::log2(analyze(corrected, 2).median / refStats.median), 0, .015,
                      "camera cast exposure corrected");
        }
        auto expOnly = matchReference(target, linear, reference, MatchMode::Exposure);
        require(expOnly.exposureMatched && !expOnly.whiteBalanceMatched, "exposure only mode");
        checkNear(expOnly.settings.temperature, 0, 0, "exposure mode preserves temperature");
        auto wbOnly = matchReference(target, linear, reference, MatchMode::WhiteBalance);
        require(wbOnly.whiteBalanceMatched && !wbOnly.exposureMatched, "WB only mode");
        checkNear(wbOnly.settings.exposure, 0, 0, "WB mode preserves exposure");
        Settings manual = linear;
        manual.shadows = .25;
        manual.midtones = -.2;
        manual.lights = .17;
        manual.highlights = -.3;
        manual.contrast = 1.1;
        manual.pivot = .2;
        manual.saturation = 1.1;
        manual.vibrance = 10;
        auto retained = matchReference(target, manual, reference, MatchMode::WBAndExposure);
        checkNear(retained.settings.shadows, manual.shadows, 0, "match leaves shadow control");
        checkNear(retained.settings.contrast, manual.contrast, 0, "match leaves contrast");
        checkNear(retained.settings.saturation, manual.saturation, 0, "match leaves saturation");
        checkNear(retained.settings.pivot, manual.pivot, 0, "match leaves pivot");
        for (int space = 0; space < 3; ++space) {
            Settings s = linear;
            s.space = space;
            std::vector<V3> in;
            for (auto c : target)
                in.push_back(convertLinear(c, 2, space));
            auto cross = matchReference(in, s, reference, MatchMode::WBAndExposure);
            checkNear(cross.settings.exposure, 1.65, .025, "cross encoding exposure");
            checkNear(cross.settings.temperature, 28, 1, "cross encoding temperature");
        }
        auto identical = makeReference(input, linear, "Same", 0);
        auto neutral = matchReference(input, linear, identical, MatchMode::WBAndExposure);
        checkNear(neutral.settings.exposure, 0, 1.e-5, "identity reference exposure");
        checkNear(neutral.settings.temperature, 0, .01, "identity reference WB");
        auto stage = std::vector<V3>(4096, {.4, .005, .01});
        auto stageRef = makeReference(stage, linear, "Stage", 1);
        auto rejected = matchReference(stage, linear, stageRef, MatchMode::WhiteBalance);
        require(!rejected.applied(), "saturated lighting refuses speculative WB");
        auto exposureStage = matchReference(stage, linear, stageRef, MatchMode::WBAndExposure);
        require(exposureStage.exposureMatched && !exposureStage.whiteBalanceMatched,
                "WB rejection still permits exposure match");
        auto black = std::vector<V3>(4096, {0, 0, 0});
        require(!matchReference(black, linear, reference, MatchMode::WBAndExposure).applied(),
                "black target rejected");
        auto blackRef = makeReference(black, linear, "Black", 0);
        require(!matchReference(input, linear, blackRef, MatchMode::Exposure).applied(),
                "black reference rejected");
        auto outliers = target;
        outliers[0] = {10000, 10000, 10000};
        auto robust = matchReference(outliers, linear, reference, MatchMode::Exposure);
        checkNear(robust.settings.exposure, expOnly.settings.exposure, .02,
                  "isolated highlight does not drive match");
        auto distantTarget = target;
        for (auto &c : distantTarget)
            c = mul(c, .0001);
        auto bounded = matchReference(distantTarget, linear, reference, MatchMode::Exposure);
        require(bounded.settings.exposure <= 5, "exposure remains bounded");
        require(bounded.exposureMatched && bounded.limited,
                "unreachable reference reports a bounded correction");
        Settings invalid = linear;
        invalid.exposure = NAN;
        require(!matchReference(target, invalid, reference, MatchMode::WBAndExposure).applied(),
                "invalid manual values rejected");
        Scratch scratch;
        // Exercise wide Windows paths, spaces and UTF-8 Unix paths as well as
        // non-ASCII reference names. Never touch the real user library.
        ReferenceStore store(scratch.path / std::filesystem::u8path("Референсы A"));
        {
            test::Environment isolated("RENZU_REFERENCE_DIRECTORY", store.directory());
            require(ReferenceStore::defaultDirectory() == store.directory(),
                    "reference directory override supports Unicode and spaces");
        }
        require(store.list().empty(), "new library empty");
        require(!std::filesystem::exists(store.directory()), "listing does not create library");
        auto saved = store.save(reference);
        require(saved.slot == 1 && !saved.id.empty(), "save allocates stable slot");
        auto encoded = serializeReference(saved);
        auto decoded = deserializeReference(encoded);
        require(decoded.name == saved.name && decoded.id == saved.id && decoded.time == saved.time,
                "portable reference metadata");
        checkNear(decoded.pixels[0].r, saved.pixels[0].r, 0, "lossless float serialization");
        ReferenceStore reopened(store.directory());
        auto loaded = reopened.load(0);
        require(loaded.id == saved.id, "latest pointer survives reopening store");
        require(reopened.load(1).id == saved.id, "load exact slot");
        auto corrupted = encoded;
        corrupted[corrupted.size() / 2] ^= 1;
        bool threw = false;
        try {
            deserializeReference(corrupted);
        } catch (const std::exception &) {
            threw = true;
        }
        require(threw, "checksum rejects corruption");
        threw = false;
        try {
            deserializeReference(std::string(300000, 'x'));
        } catch (const std::exception &) {
            threw = true;
        }
        require(threw, "oversized reference rejected");
        auto second = store.save(reference);
        require(second.slot == 2, "new save does not overwrite prior reference");
        auto replaced = store.save(reference, 1);
        require(replaced.slot == 1 && replaced.id != saved.id,
                "explicit slot replacement gets new identity");
        require(deserializeReference(encoded).id == saved.id,
                "project snapshot retains original after library replacement");
        auto concurrent = [&] { return store.save(reference); };
        auto one = std::async(std::launch::async, concurrent),
             two = std::async(std::launch::async, concurrent);
        auto a = one.get(), b = two.get();
        require(a.slot != b.slot && a.id != b.id, "concurrent saves allocate distinct slots");
        // Separate processes do not share the C++ mutex. The filesystem lock
        // must still serialize allocation and preserve both newly saved slots.
        auto countBefore = store.list().size();
#ifdef _WIN32
        auto executable = std::filesystem::absolute(argv[0]);
        test::ReferenceWriter first(executable, store.directory()),
            secondWriter(executable, store.directory());
        require(first.wait(), "first process reference writer completed");
        require(secondWriter.wait(), "second process reference writer completed");
#else
        pid_t children[2];
        for (auto &child : children) {
            child = fork();
            require(child >= 0, "spawn reference writer");
            if (child == 0) {
                try {
                    store.save(reference);
                    _exit(0);
                } catch (...) {
                    _exit(1);
                }
            }
        }
        for (auto child : children) {
            int status = 0;
            require(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                        WEXITSTATUS(status) == 0,
                    "process reference writer completed");
        }
#endif
        require(store.list().size() == countBefore + 2,
                "cross-process save allocation preserves both entries");
        while (store.list().size() < referenceSlots)
            store.save(reference);
        threw = false;
        try {
            store.save(reference);
        } catch (const std::exception &) {
            threw = true;
        }
        require(threw, "full library requires explicit replacement");
        std::ofstream(store.directory() / "reference-3.rgr", std::ios::trunc) << "bad data";
        require(store.list().size() == referenceSlots - 1,
                "corrupt reference isolated from valid entries");
        threw = false;
        try {
            store.load(3);
        } catch (const std::exception &) {
            threw = true;
        }
        require(threw, "explicit corrupt load reports error");
        std::cout << "PASS: " << checks
                  << " reference matching and persistent library assertions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
