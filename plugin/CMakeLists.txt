# Dvlce Capsule JUCE plugin.  JUCE 8.0.13 (fetched; or a local checkout via FETCHCONTENT_SOURCE_DIR_JUCE).
# JUCE has its own licence (AGPLv3 or a JUCE licence): see README.md "Building the plugin".
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.13
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

set(DVLCE_CAPSULE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DVLCE_CAPSULE_FORMATS AU)
endif()

juce_add_plugin(DvlceCapsule
    COMPANY_NAME                "DVLCE"
    COMPANY_WEBSITE             "https://github.com/toyuvalo/dvlce-capsule"
    BUNDLE_ID                   "ca.dvlce.DvlceCapsule"
    PLUGIN_MANUFACTURER_CODE    Dvlc
    PLUGIN_CODE                 Dcap
    FORMATS                     ${DVLCE_CAPSULE_FORMATS}
    PRODUCT_NAME                "Dvlce Capsule"
    VERSION                     "${PROJECT_VERSION}"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    HARDENED_RUNTIME_ENABLED    FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "Dvlce Capsule (standalone) records the two capsules of your microphone."
    DESCRIPTION                 "Dual-capsule microphone: virtual polar patterns"
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx Tools)

target_sources(DvlceCapsule PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)

target_compile_definitions(DvlceCapsule PUBLIC
    DVLCE_CAPSULE_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DvlceCapsule
    PRIVATE
        dvlce_capsule_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Offscreen editor snapshot (a GUI check without a window): DvlceCapsuleSnapshot <out.png> [voicing index]
add_executable(DvlceCapsuleSnapshot tools/editor_snapshot.cpp)
target_link_libraries(DvlceCapsuleSnapshot PRIVATE DvlceCapsule)
target_include_directories(DvlceCapsuleSnapshot PRIVATE "${juce_SOURCE_DIR}/modules")

# Vendored Quantum UI and OFL typefaces; no private repository dependency.
juce_add_binary_data(DvlceUiFonts NAMESPACE DvlceUiFonts HEADER_NAME DvlceUiFonts.h SOURCES
    Source/ui/fonts/Rajdhani-Medium.ttf Source/ui/fonts/Rajdhani-SemiBold.ttf
    Source/ui/fonts/BarlowSemiCondensed-Medium.ttf Source/ui/fonts/BarlowSemiCondensed-SemiBold.ttf)
juce_add_binary_data(DvlceFactory NAMESPACE DvlceFactory HEADER_NAME DvlceFactory.h SOURCES ../presets/factory.json)
target_sources(DvlceCapsule PRIVATE Source/ui/Theme.cpp Source/ui/DvlceLookAndFeel.cpp)
target_link_libraries(DvlceCapsule PUBLIC DvlceUiFonts DvlceFactory)
if(MSVC)
    target_compile_options(DvlceCapsule PRIVATE /utf-8)
endif()

add_executable(DvlceCapsulePresetTest tools/preset_test.cpp)
target_link_libraries(DvlceCapsulePresetTest PRIVATE DvlceCapsule)
target_include_directories(DvlceCapsulePresetTest PRIVATE "${juce_SOURCE_DIR}/modules")
