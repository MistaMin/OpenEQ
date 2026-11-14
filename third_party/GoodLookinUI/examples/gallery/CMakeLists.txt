# Gallery app: needs JUCE (juce_add_gui_app), so it is only added when the host project supplies JUCE.
juce_add_gui_app(GoodLookinUIGallery PRODUCT_NAME "GoodLookinUI Gallery" VERSION ${PROJECT_VERSION})
target_compile_definitions(GoodLookinUIGallery PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="GoodLookinUI Gallery" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}" GOODLOOKINUI_ENABLE_EDITOR=1)
target_sources(GoodLookinUIGallery PRIVATE Main.cpp)
target_link_libraries(GoodLookinUIGallery PRIVATE goodlookinui_juce juce::juce_gui_basics juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Headless check: renders every page, verifies it is not blank and that animation changes it.
if(GOODLOOKINUI_BUILD_TESTS)
  add_test(NAME goodlookinui_gallery_selftest COMMAND GoodLookinUIGallery --selftest ${CMAKE_CURRENT_BINARY_DIR}/gallery-shots)
endif()
