/*  Dialog Bubble Detector
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#ifndef PokemonAutomation_PokemonSV_DialogBubbleDetector_H
#define PokemonAutomation_PokemonSV_DialogBubbleDetector_H

#include <vector>
#include "Common/Cpp/Color.h"
#include "Common/Cpp/Containers/FixedLimitVector.h"
#include "CommonFramework/ImageTools/ImageBoxes.h"
#include "CommonFramework/InferenceInfra/VisualInferenceCallback.h"
#include "CommonFramework/Inference/VisualDetector.h"

namespace PokemonAutomation{

class VideoOverlaySet;
class VideoOverlay;
class OverlayBoxScope;

namespace NintendoSwitch{
namespace PokemonSV{

// Detect one or more yellow dialog bubbles with exactly two lines of text in the overworld.
// In case of overlapping bubbles only one is detected.
class DialogBubbleDetector : public StaticScreenDetector{
public:
    DialogBubbleDetector(Color color, const ImageFloatBox& box);

    virtual void make_overlays(VideoOverlaySet& items) const override;
    virtual bool detect(const ImageViewRGB32& screen) const override;

    std::vector<ImageFloatBox> detect_all(const ImageViewRGB32& screen) const;

protected:
    Color m_color;
    ImageFloatBox m_box;
};


// Watch for at least one yellow dialog bubble to appear
class DialogBubbleWatcher : public VisualInferenceCallback{
public:
    ~DialogBubbleWatcher();
    DialogBubbleWatcher(Color color, VideoOverlay& overlay, const ImageFloatBox& box);

    virtual void make_overlays(VideoOverlaySet& items) const override;
    virtual bool process_frame(const ImageViewRGB32& frame, WallClock timestamp) override;

    const std::vector<ImageFloatBox>& found_locations() const { return m_hits; }


protected:
    VideoOverlay& m_overlay;
    DialogBubbleDetector m_detector;
    std::vector<ImageFloatBox> m_hits;
    FixedLimitVector<OverlayBoxScope> m_hit_boxes;
    
};




}
}
}
#endif
