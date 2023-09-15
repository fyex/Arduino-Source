/*  Map Menu Detector
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#include "CommonFramework/ImageTypes/ImageViewRGB32.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
#include "CommonFramework/ImageTools/ImageStats.h"
#include "CommonFramework/ImageTools/SolidColorTest.h"
#include "PokemonSV_MapMenuDetector.h"

//#include <iostream>
//using std::cout;
//using std::endl;

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonSV{


MapFlyMenuDetector::MapFlyMenuDetector(Color color)
    : m_color(color)
    , m_middle_box(0.523, 0.680, 0.080, 0.010)
    , m_bottom_box(0.523, 0.744, 0.080, 0.020)
{}

void MapFlyMenuDetector::make_overlays(VideoOverlaySet& items) const{
    items.add(m_color, m_middle_box);
    items.add(m_color, m_bottom_box);
}

bool MapFlyMenuDetector::detect(const ImageViewRGB32& screen) const{
    const ImageStats stats0 = image_stats(extract_box_reference(screen, m_middle_box));
    if (!is_white(stats0)){
        return false;
    }
    const ImageStats stats1 = image_stats(extract_box_reference(screen, m_bottom_box));
    return is_white(stats1);
}


MapFlyMenuWatcher::~MapFlyMenuWatcher() = default;

MapFlyMenuWatcher::MapFlyMenuWatcher(Color color, VideoOverlay& overlay)
    : VisualInferenceCallback("MapFlyMenuWatcher")
    , m_overlay(overlay)
    , m_detector(color)
{}

void MapFlyMenuWatcher::make_overlays(VideoOverlaySet& items) const{
    m_detector.make_overlays(items);
}

bool MapFlyMenuWatcher::process_frame(const ImageViewRGB32& screen, WallClock timestamp){
    return m_detector.detect(screen);
}


MapDestinationMenuDetector::MapDestinationMenuDetector(Color color)
    : m_color(color)
    , m_bottom_box(0.523, 0.670, 0.080, 0.020)
    , m_fly_menu_box(0.523, 0.744, 0.080, 0.020)
{}

void MapDestinationMenuDetector::make_overlays(VideoOverlaySet& items) const{
    items.add(m_color, m_bottom_box);
    items.add(m_color, m_fly_menu_box);
}

bool MapDestinationMenuDetector::detect(const ImageViewRGB32& screen) const{
    const ImageStats stats0 = image_stats(extract_box_reference(screen, m_bottom_box));
    if (!is_white(stats0)){
        return false;
    }
    const ImageStats stats1 = image_stats(extract_box_reference(screen, m_fly_menu_box));
    return !is_white(stats1);
}


MapDestinationMenuWatcher::~MapDestinationMenuWatcher() = default;

MapDestinationMenuWatcher::MapDestinationMenuWatcher(Color color, VideoOverlay& overlay)
    : VisualInferenceCallback("MapDestinationMenuWatcher")
    , m_overlay(overlay)
    , m_detector(color)
{}

void MapDestinationMenuWatcher::make_overlays(VideoOverlaySet& items) const{
    m_detector.make_overlays(items);
}

bool MapDestinationMenuWatcher::process_frame(const ImageViewRGB32& screen, WallClock timestamp){
    return m_detector.detect(screen);
}






}
}
}