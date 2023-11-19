/*  Dialog Bubble Detector
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#include "CommonFramework/ImageTools/BinaryImage_FilterRgb32.h"
#include "CommonFramework/ImageTools/ImageFilter.h"
#include "CommonFramework/ImageTools/ImageStats.h"
#include "CommonFramework/ImageTools/SolidColorTest.h"
#include "CommonFramework/ImageTypes/ImageRGB32.h"
#include "CommonFramework/ImageTypes/ImageViewRGB32.h"
#include "CommonFramework/ImageTypes/BinaryImage.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
#include "Kernels/Waterfill/Kernels_Waterfill_Session.h"
#include "Kernels/Waterfill/Kernels_Waterfill_Types.h"
#include "PokemonSV_DialogBubbleDetector.h"
#include "Common/Cpp/PrettyPrint.h"
#include <iostream>


namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonSV{


DialogBubbleDetector::DialogBubbleDetector(Color color, const ImageFloatBox& box)
    : m_color(color)
    , m_box(box)
{}

void DialogBubbleDetector::make_overlays(VideoOverlaySet& items) const{
    items.add(m_color, m_box);
}

bool DialogBubbleDetector::detect(const ImageViewRGB32& screen) const{
    std::vector<ImageFloatBox> hits = detect_all(screen);
    return !hits.empty();
}

std::vector<ImageFloatBox> DialogBubbleDetector::detect_all(const ImageViewRGB32& screen) const{
    using namespace Kernels::Waterfill;

    uint32_t MIN_BORDER_THRESHOLD = 0xffc07000; //orange border
    uint32_t MAX_BORDER_THRESHOLD = 0xffffc550; //orange border

    uint32_t MIN_YELLOW_THRESHOLD = 0xffd0b000;
    uint32_t MAX_YELLOW_THRESHOLD = 0xffffff30;


    const double screen_rel_width = (screen.width() / 1920.0);
    const double screen_rel_height = (screen.height() / 1080.0);

    const double min_object_width = 100.0;//180.0;
    const double min_object_height = 70.0;

    const size_t min_width = size_t(screen_rel_width * min_object_width);
    const size_t min_height = size_t(screen_rel_height * min_object_height);

    std::vector<ImageFloatBox> dialog_boxes;
    {
        // First session: find orange objects
        PackedBinaryMatrix border_matrix = compress_rgb32_to_binary_range(screen, MIN_BORDER_THRESHOLD, MAX_BORDER_THRESHOLD);

        std::unique_ptr<WaterfillSession> session = make_WaterfillSession(border_matrix);

        auto iter = session->make_iterator(50);
        WaterfillObject object;
        while (iter->find_next(object, true)) {
            //  Discard small objects
            if (object.width() < min_width || object.height() < min_height) // object is too small
            {
                continue;
            }

            // check for yellow inside the orange border
            ImagePixelBox border_pixel_box(object);
            ImageFloatBox border_float_box = pixelbox_to_floatbox(screen, border_pixel_box);
            ImageViewRGB32 dialog = extract_box_reference(screen, border_pixel_box);

            //dialog.save("./DebugDumps/auction_farmer/" + now_to_filestring() + "_orange.png");

            // Second session: find yellow objects within the found orange objects
            PackedBinaryMatrix yellow_matrix = compress_rgb32_to_binary_range(dialog, MIN_YELLOW_THRESHOLD, MAX_YELLOW_THRESHOLD);
            ImageRGB32 black_white_image = to_blackwhite_rgb32_range(screen, MIN_BORDER_THRESHOLD, MAX_BORDER_THRESHOLD, true);
            //black_white_image.save("./DebugDumps/auction_farmer/" + now_to_filestring() + "_black.png");

            std::unique_ptr<WaterfillSession> yellow_session = make_WaterfillSession(yellow_matrix);
            auto yellow_iter = yellow_session->make_iterator(300);
            WaterfillObject yellow_object;

            while (yellow_iter->find_next(yellow_object, true)) {
                ImagePixelBox dialog_pixel_box(yellow_object);
                ImageFloatBox translated_dialog_box = translate_to_parent(screen, border_float_box, dialog_pixel_box);
                double min_x = translated_dialog_box.x;
                double max_x = min_x + translated_dialog_box.width;
                double min_y = translated_dialog_box.y;
                double max_y = min_y + translated_dialog_box.height;

                double border_thickness_x = 0.0010;
                double border_thickness_y = 0.0010;
                double corner_size_x = 0.021;//0.021;
                double corner_size_y = 0.03;//0.035;

                //  Discard small objects and object touching the edge of the screen or the mini map area (potentially incomplete dialog bubbles)
                if (min_x < border_thickness_x || min_y < border_thickness_y // touching left or top edge
                    || max_x >= 1.0 - border_thickness_x || max_y >= 1.0 - border_thickness_y // touching right or bottom edge
                    || (max_x >= 0.82 - border_thickness_x && max_y >= 0.68 - border_thickness_y) // touches mini map area
                    || translated_dialog_box.width < 0.052 || translated_dialog_box.height < 0.065 // object is too small
                    || translated_dialog_box.width * screen.width() < translated_dialog_box.height * screen.height() // wrong aspect ratio
                    || translated_dialog_box.height > 0.13 // object is too big
                )
                {
                    continue;
                }

                //ImageViewRGB32 yellow = extract_box_reference(screen, translated_dialog_box);
                //yellow.save("./DebugDumps/auction_farmer/" + now_to_filestring() + "_yellow.png");

                // Test for orange border
                std::vector<ImageFloatBox> border_boxes;
                border_boxes.emplace_back(ImageFloatBox(min_x - border_thickness_x, min_y + corner_size_y, border_thickness_x, translated_dialog_box.height - 2 * corner_size_y)); // left border
                border_boxes.emplace_back(ImageFloatBox(max_x, min_y + corner_size_y, border_thickness_x, translated_dialog_box.height - 2 * corner_size_y)); // right border
                border_boxes.emplace_back(ImageFloatBox(min_x + corner_size_x, min_y - border_thickness_y, translated_dialog_box.width - 2 * corner_size_x, border_thickness_y)); // top border
                border_boxes.emplace_back(ImageFloatBox(min_x + corner_size_x, max_y, translated_dialog_box.width - 2 * corner_size_x, border_thickness_y)); // bottom border

                bool is_bad_border = false;
                for (ImageFloatBox border_box : border_boxes) {
                    ImageViewRGB32 border = extract_box_reference(black_white_image, border_box);
                    //border.save("./DebugDumps/auction_farmer/" + now_to_filestring() + "_border.png");
                    FloatPixel average = image_average(border);
                    if (average.r > 50.0) {
                        is_bad_border = true;
                        break;
                    }
                }
                if (is_bad_border) {
                    continue;
                }

                // Only add the new box if it is not overlapping with an already found box
                bool is_overlapping = false;
                for (size_t i = 0; i < dialog_boxes.size(); i++) {
                    ImageFloatBox found_box = dialog_boxes[i];
                    is_overlapping = is_overlapping
                        || (min_x <= found_box.x + found_box.width && found_box.x <= max_x && min_y <= found_box.y + found_box.height && found_box.y <= max_y);
                }
                if (is_overlapping) {
                    continue;
                }

                dialog_boxes.emplace_back(translated_dialog_box);
            }
        }
    }
    return dialog_boxes;
}





DialogBubbleWatcher::~DialogBubbleWatcher() = default;

DialogBubbleWatcher::DialogBubbleWatcher(Color color, VideoOverlay& overlay, const ImageFloatBox& box)
    : VisualInferenceCallback("DialogBubbleWatcher")
    , m_overlay(overlay)
    , m_detector(color, box)
{}

void DialogBubbleWatcher::make_overlays(VideoOverlaySet& items) const{
    m_detector.make_overlays(items);
}

bool DialogBubbleWatcher::process_frame(const ImageViewRGB32& screen, WallClock timestamp){
    m_hits = m_detector.detect_all(screen);

    m_hit_boxes.reset(m_hits.size());
    for (const ImageFloatBox& hit : m_hits){
        m_hit_boxes.emplace_back(m_overlay, hit, COLOR_MAGENTA);
    }
    return !m_hits.empty();
}



}
}
}
