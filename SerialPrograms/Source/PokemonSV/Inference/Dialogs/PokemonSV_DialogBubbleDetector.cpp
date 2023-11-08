/*  Dialog Bubble Detector
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#include "CommonFramework/ImageTools/BinaryImage_FilterRgb32.h"
#include "CommonFramework/ImageTypes/ImageViewRGB32.h"
#include "CommonFramework/ImageTypes/BinaryImage.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
#include "Kernels/Waterfill/Kernels_Waterfill_Session.h"
#include "Kernels/Waterfill/Kernels_Waterfill_Types.h"
#include "PokemonSV_DialogBubbleDetector.h"

//#include "Common/Cpp/PrettyPrint.h"


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
    const double min_object_height = 100.0;

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

                //  Discard small objects and object touching the edge of the screen or the mini map area (potentially incomplete dialog bubbles)
                if (min_x < 0.0001 || min_y < 0.0001 // touching left or top edge
                    || max_x > 0.9999 || max_y > 0.9999 // touching right or bottom edge
                    || (max_x > 0.82 && max_y > 0.68) // touches mini map area
                    || translated_dialog_box.width < 0.052 || translated_dialog_box.height < 0.0926 // object is too small
                    || translated_dialog_box.width * screen.width() < translated_dialog_box.height * screen.height() // wrong aspect ratio
                    || translated_dialog_box.height > 0.13 // object is too big
                )
                {
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

                //ImageViewRGB32 image = extract_box_reference(screen, translated_dialog_box);
                //image.save("./DebugDumps/sv_auction/" + now_to_filestring() + "_yellow.png");
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
