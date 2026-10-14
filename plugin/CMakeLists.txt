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
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DvlceCapsule
    PRIVATE
        dvlce_capsule_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
