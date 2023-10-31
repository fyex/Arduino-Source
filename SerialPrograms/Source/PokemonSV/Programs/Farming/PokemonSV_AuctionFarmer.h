/*  Auction Farmer
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#ifndef PokemonAutomation_PokemonSV_AuctionFarmer_H
#define PokemonAutomation_PokemonSV_AuctionFarmer_H

#include "Common/Cpp/Options/BooleanCheckBoxOption.h"
#include "Common/Cpp/Options/FloatingPointOption.h"
#include "Common/Cpp/Options/StaticTextOption.h"
#include "CommonFramework/ImageTools/ImageBoxes.h"
#include "CommonFramework/Notifications/EventNotificationsTable.h"
#include "CommonFramework/Options/LanguageOCROption.h"
#include "PokemonSV/Options/PokemonSV_AuctionItemTable.h"
#include "NintendoSwitch/NintendoSwitch_SingleSwitchProgram.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonSV{

enum class AuctionPosition {
    UNDEFINED,
    VENDOR_1, // north-east: balls
    VENDOR_2, // north-west: medicine, vitamins
    VENDOR_3, // south-east: feathers, ev berries
    VENDOR_4, // south-west: special items
    EAST,
    WEST,
    NORTH,
    SOUTH,
    CENTER
};

struct AuctionOffer {
    std::string item;
    //AuctionPosition position;
};


class AuctionFarmer_Descriptor : public SingleSwitchProgramDescriptor{
public:
    AuctionFarmer_Descriptor();

    struct Stats;
    virtual std::unique_ptr<StatsTracker> make_stats() const override;

};




class AuctionFarmer : public SingleSwitchProgramInstance{
public:
    AuctionFarmer();
    virtual void program(SingleSwitchProgramEnvironment& env, BotBaseContext& context) override;

private:
    OCR::LanguageOCROption LANGUAGE;
    BooleanCheckBoxOption ONE_NPC;
    AuctionItemTable TARGET_ITEMS;
    
    EventNotificationOption NOTIFICATION_STATUS_UPDATE;
    EventNotificationOption NOTIFICATION_AUCTION_WIN;
    EventNotificationsOption NOTIFICATIONS;

    SectionDividerOption m_advanced_options;
    FloatingPointOption OPTIMAL_X;
    FloatingPointOption X_ALPHA;

    std::vector<ImageFloatBox> detect_dialog_boxes(const ImageViewRGB32& screen);
    void reset_auctions(SingleSwitchProgramEnvironment& env, BotBaseContext& context, bool do_full_reset, uint8_t& year, bool current_position_is_east);
    std::vector<std::pair<AuctionOffer, ImageFloatBox>> check_offers(SingleSwitchProgramEnvironment& env, BotBaseContext& context);
    bool are_offers_equal(std::vector<std::pair<AuctionOffer, ImageFloatBox>>& first_offers, std::vector<std::pair<AuctionOffer, ImageFloatBox>>& second_offers);

    void move_to_auctioneer(SingleSwitchProgramEnvironment& env, BotBaseContext& context, AuctionOffer wanted, bool& is_new_position_east);
    std::pair<AuctionOffer, ImageFloatBox> find_target(SingleSwitchProgramEnvironment& env, BotBaseContext& context, AuctionOffer wanted);
    void find_target_bubble_center(SingleSwitchProgramEnvironment& env, BotBaseContext& context, AuctionOffer wanted, float& center_x, float& center_y);
    void get_bubble_center(ImageFloatBox bubble, float& center_x, float& center_y);
    void move_to_dialog(SingleSwitchProgramEnvironment& env, BotBaseContext& context, AuctionOffer offer, bool& is_new_position_east);

    bool is_good_dialog_center(float center_x, float center_y);

    std::vector<std::pair<float, float>> detect_sorted_dialog_centers(SingleSwitchProgramEnvironment& env, BotBaseContext& context);

    void bid_on_item(SingleSwitchProgramEnvironment& env, BotBaseContext& context, AuctionOffer offer);
    bool is_good_offer(AuctionOffer);
    void reset_orientation(SingleSwitchProgramEnvironment& env, BotBaseContext& context, bool character_orientation);
    bool reset_position(SingleSwitchProgramEnvironment& env, BotBaseContext& context, bool move_down);
};




}
}
}
#endif
