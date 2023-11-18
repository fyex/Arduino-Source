/*  Auction Farmer
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#include "CommonFramework/GlobalSettingsPanel.h"
#include "CommonFramework/Exceptions/FatalProgramException.h"
#include "CommonFramework/Exceptions/OperationFailedException.h"
#include "CommonFramework/InferenceInfra/InferenceRoutines.h"
#include "CommonFramework/Notifications/ProgramNotifications.h"
#include "CommonFramework/OCR/OCR_NumberReader.h"
#include "CommonFramework/Tools/StatsTracking.h"
#include "CommonFramework/VideoPipeline/VideoFeed.h"
#include "CommonFramework/VideoPipeline/VideoOverlay.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
#include "CommonFramework/Tools/VideoResolutionCheck.h"
#include "Pokemon/Pokemon_Strings.h"
#include "PokemonSV/PokemonSV_Settings.h"
#include "PokemonSV/Inference/Dialogs/PokemonSV_DialogBubbleDetector.h"
#include "PokemonSV/Inference/Dialogs/PokemonSV_DialogDetector.h"
#include "PokemonSV/Inference/PokemonSV_AuctionItemNameReader.h"
#include "PokemonSV/Inference/Overworld/PokemonSV_OverworldDetector.h"
#include "PokemonSV/Programs/PokemonSV_GameEntry.h"
#include "PokemonSV/Programs/PokemonSV_Navigation.h"
#include "PokemonSV/Programs/PokemonSV_SaveGame.h"
#include "PokemonSV/Resources/PokemonSV_AuctionItemNames.h"
#include "PokemonSwSh/Commands/PokemonSwSh_Commands_DateSpam.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "PokemonSV_AuctionFarmer.h"

#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonSV{

using namespace Pokemon;


AuctionFarmer_Descriptor::AuctionFarmer_Descriptor()
    : SingleSwitchProgramDescriptor(
        "PokemonSV:AuctionFarmer",
        STRING_POKEMON + " SV", "Auction Farmer",
        "ComputerControl/blob/master/Wiki/Programs/PokemonSV/AuctionFarmer.md",
        "Check auctions and bid on items.",
        FeedbackType::REQUIRED,
        AllowCommandsWhenRunning::DISABLE_COMMANDS,
        PABotBaseLevel::PABOTBASE_12KB
    )
{}

struct AuctionFarmer_Descriptor::Stats : public StatsTracker {
    Stats()
        : m_resets(m_stats["Resets"])
        , m_auctions(m_stats["Auctions"])
        , m_money(m_stats["Spent Money"])
        , m_errors(m_stats["Errors"])
    {
        m_display_order.emplace_back("Resets");
        m_display_order.emplace_back("Auctions");
        m_display_order.emplace_back("Spent Money");
        m_display_order.emplace_back("Errors", HIDDEN_IF_ZERO);
    }
    std::atomic<uint64_t>& m_resets;
    std::atomic<uint64_t>& m_auctions;
    std::atomic<uint64_t>& m_money;
    std::atomic<uint64_t>& m_errors;
};
std::unique_ptr<StatsTracker> AuctionFarmer_Descriptor::make_stats() const{
    return std::unique_ptr<StatsTracker>(new Stats());
}


AuctionFarmer::AuctionFarmer()
    : LANGUAGE(
        "<b>Game Language:</b><br>The language is needed to read which items are offered.",
        AuctionItemNameReader::instance().languages(),
        LockMode::LOCK_WHILE_RUNNING,
        true
    )
    , ONE_NPC("<b>One NPC:</b><br>Check only the NPC you're standing in front of.", LockMode::LOCK_WHILE_RUNNING, true)
    , TARGET_ITEMS("<b>Items:</b><br>Multiple Items can be selected. The program will bid on any selected item which is offered.")
    , NOTIFICATION_STATUS_UPDATE("Status Update", true, false, std::chrono::seconds(3600))
    , NOTIFICATION_AUCTION_WIN("Auction Win", true, false, ImageAttachmentMode::JPG, {"Notifs"})
    , NOTIFICATIONS({
                    &NOTIFICATION_STATUS_UPDATE,
                    &NOTIFICATION_AUCTION_WIN,
                    &NOTIFICATION_PROGRAM_FINISH,
                    &NOTIFICATION_ERROR_RECOVERABLE,
                    &NOTIFICATION_ERROR_FATAL,
})
    , m_advanced_options("<font size=4><b>Advanced Options: (developer only)</b></font>")
    , OPTIMAL_X("<b>Optimal x:</b> Intended x-coordinate for the center of dialog bubbles.", LockMode::LOCK_WHILE_RUNNING, 0.26, 0.0, 1.0)
    , X_ALPHA("<b>x alpha:</b> Threshold for the acceptable x-coordinate range.", LockMode::LOCK_WHILE_RUNNING, 0.02, 0.0, 1.0)
    , OPTIMAL_Y("<b>Optimal y:</b> Intended y-coordinate for the center of dialog bubbles.", LockMode::LOCK_WHILE_RUNNING, 0.24, 0.0, 1.0)
    , Y_ALPHA("<b>y alpha:</b> Threshold for the acceptable y-coordinate range.", LockMode::LOCK_WHILE_RUNNING, 0.02, 0.0, 1.0)
{
    PA_ADD_OPTION(LANGUAGE);
    PA_ADD_OPTION(ONE_NPC);
    PA_ADD_OPTION(TARGET_ITEMS);
    PA_ADD_OPTION(NOTIFICATIONS);
    if (PreloadSettings::instance().DEVELOPER_MODE) {
        PA_ADD_STATIC(m_advanced_options);
        PA_ADD_OPTION(OPTIMAL_X);
        PA_ADD_OPTION(X_ALPHA);
        PA_ADD_OPTION(OPTIMAL_Y);
        PA_ADD_OPTION(Y_ALPHA);
    }
}


std::vector<ImageFloatBox> AuctionFarmer::detect_dialog_boxes(const ImageViewRGB32& screen) {
    DialogBubbleDetector detector(COLOR_GREEN, ImageFloatBox(0.0, 0.0, 0.999, 0.999));
    std::vector<ImageFloatBox> dialog_boxes = detector.detect_all(screen);
    return dialog_boxes;
}


void AuctionFarmer::reset_auctions(SingleSwitchProgramEnvironment& env, BotBaseContext& context, bool do_full_reset, uint8_t& year, bool current_postion_is_east){
    try{
        if (do_full_reset){
            if (year == MAX_YEAR){
                pbf_press_button(context, BUTTON_HOME, 10, GameSettings::instance().GAME_TO_HOME_DELAY);
                home_roll_date_enter_game_autorollback(env.console, context, year);
            }
            save_game_from_overworld(env.program_info(), env.console, context);

            pbf_press_button(context, BUTTON_HOME, 10, GameSettings::instance().GAME_TO_HOME_DELAY);
            home_roll_date_enter_game_autorollback(env.console, context, year);
        }
        pbf_wait(context, 1 * TICKS_PER_SECOND);
        context.wait_for_all_requests();

        reset_game(env.program_info(), env.console, context);
    }catch (OperationFailedException& e){
        AuctionFarmer_Descriptor::Stats& stats = env.current_stats<AuctionFarmer_Descriptor::Stats>();
        stats.m_errors++;
        env.update_stats();
        throw FatalProgramException(std::move(e));
    }
}


std::vector<std::pair<AuctionOffer, ImageFloatBox>> AuctionFarmer::check_offers(SingleSwitchProgramEnvironment& env, BotBaseContext& context) {
    AuctionFarmer_Descriptor::Stats& stats = env.current_stats<AuctionFarmer_Descriptor::Stats>();

    pbf_wait(context, 2 * TICKS_PER_SECOND);
    context.wait_for_all_requests();
    
    VideoSnapshot screen = env.console.video().snapshot();
    std::vector<ImageFloatBox> dialog_boxes = detect_dialog_boxes(screen);
    std::vector<std::pair<AuctionOffer, ImageFloatBox>> offers;

    if (dialog_boxes.empty()) {
        stats.m_errors++;
        send_program_recoverable_error_notification(env, NOTIFICATION_ERROR_RECOVERABLE, "Could not detect any offer dialogs.", screen);
    }

    // read dialog bubble
    for (ImageFloatBox dialog_box : dialog_boxes){
        OverlayBoxScope dialog_overlay(env.console, dialog_box, COLOR_DARK_BLUE);

        ImageFloatBox offer_box(0.05, 0.02, 0.90, 0.49);
        ImageFloatBox translated_offer_box = translate_to_parent(screen, dialog_box, floatbox_to_pixelbox(dialog_box.width, dialog_box.height, offer_box));
        OverlayBoxScope offer_overlay(env.console, translated_offer_box, COLOR_BLUE);
        
        ImageViewRGB32 dialog = extract_box_reference(screen, dialog_box);
        ImageViewRGB32 offer_image = extract_box_reference(dialog, offer_box);

        const double LOG10P_THRESHOLD = -1.5;
        std::string best_item;
        OCR::StringMatchResult result = AuctionItemNameReader::instance().read_substring(
            env.console, LANGUAGE,
            offer_image,
            OCR::BLACK_TEXT_FILTERS()
        );

        result.clear_beyond_log10p(LOG10P_THRESHOLD);
        if (best_item.empty() && !result.results.empty()) {
            auto iter = result.results.begin();
            if (iter->first < LOG10P_THRESHOLD) {
                best_item = iter->second.token;

                AuctionOffer offer{ best_item };
                std::pair<AuctionOffer, ImageFloatBox> pair(offer, dialog_box);
                offers.emplace_back(pair);
            }
        }
    }
    return offers;
}

bool AuctionFarmer::are_offers_equal(std::vector<std::pair<AuctionOffer, ImageFloatBox>>& first_offers, std::vector<std::pair<AuctionOffer, ImageFloatBox>>& second_offers) {
    if (first_offers.size() == second_offers.size()) {
        std::unordered_multiset<std::string> first;
        std::unordered_multiset<std::string> second;
        for (size_t i = 0; i < first_offers.size(); i++) {
            first.emplace(first_offers[i].first.item);
            second.emplace(second_offers[i].first.item);
        }
        return first == second;
    }
    return false;
}

bool AuctionFarmer::is_good_offer(AuctionOffer offer) {
    // Special handling for Japanese bottle cap items
    bool any_bottle_cap = false;
    if (LANGUAGE == Language::Japanese) {
        any_bottle_cap = (offer.item == "bottle-cap" || offer.item == "gold-bottle-cap") 
            && (TARGET_ITEMS.find_item("bottle-cap") || TARGET_ITEMS.find_item("gold-bottle-cap"));
    }

    return TARGET_ITEMS.find_item(offer.item) || any_bottle_cap ;
}

// Move to auctioneer and interact
void AuctionFarmer::move_to_auctioneer(SingleSwitchProgramEnvironment& env, BotBaseContext& context, AuctionOffer offer, bool& is_new_position_east) {
    AdvanceDialogWatcher advance_detector(COLOR_YELLOW);

    size_t tries = 0;
    while (tries < 10) {
        if (!ONE_NPC) {
            move_to_dialog(env, context, offer, is_new_position_east);
            context.wait_for_all_requests();
        }

        // interact with the NPC
        pbf_press_button(context, BUTTON_A, 20, 100);
        int ret = wait_until(env.console, context, Milliseconds(6000), { advance_detector });
        if (ret == 0) {
            return;
        }
        tries++;
    }
    throw OperationFailedException(
        ErrorReport::SEND_ERROR_REPORT, env.console,
        "Too many attempts to talk to the NPC.",
        true
    );
}

std::pair<AuctionOffer, ImageFloatBox> AuctionFarmer::find_target(SingleSwitchProgramEnvironment& env, BotBaseContext& context, AuctionOffer wanted) {
    std::vector<std::pair<AuctionOffer, ImageFloatBox>> offers = check_offers(env, context);
    for (std::pair<AuctionOffer, ImageFloatBox> offer : offers) {
        if (offer.first.item == wanted.item) {
            return offer;
        }
    }

    throw OperationFailedException(
        ErrorReport::SEND_ERROR_REPORT, env.console,
        "Lost offer dialog for wanted item: " + wanted.item,
        true
    );
}

void AuctionFarmer::get_bubble_center(ImageFloatBox bubble, float& center_x, float& center_y) {
    center_x = bubble.x + (0.5 * bubble.width);
    center_y = bubble.y + (0.5 * bubble.height);
}

void AuctionFarmer::find_target_bubble_center(SingleSwitchProgramEnvironment& env, BotBaseContext& context, AuctionOffer wanted, float& center_x, float& center_y) {
    std::pair<AuctionOffer, ImageFloatBox> target = find_target(env, context, wanted);
    get_bubble_center(target.second, center_x, center_y);
}



// Dialog is the only piece of orientation we have, so the goal is to walk next to the dialog bubble
// This is only used for multiple NPCs.
void AuctionFarmer::move_to_dialog(SingleSwitchProgramEnvironment& env, BotBaseContext& context, AuctionOffer wanted, bool& is_new_position_east) {
    env.log("Moving to NPC.");
    float center_x = 0.0f;
    float center_y = 0.0f;

    float min_y = 0.43f;
    float max_y = 0.57f;

    context.wait_for_all_requests();
    find_target_bubble_center(env, context, wanted, center_x, center_y);

    // move up or down
    uint32_t up_ticks = 0;
    uint32_t down_ticks = 0;
    while (center_y < min_y || center_y > max_y) {
        bool move_up = center_y < min_y;
        uint8_t joystick_y = move_up ? 64 : 192;
        uint16_t joystick_ticks = std::max(std::max(min_y - center_y, center_y - max_y), 0.1f) * TICKS_PER_SECOND * 5;
        if (move_up) {
            up_ticks += joystick_ticks;
        }
        else {
            down_ticks += joystick_ticks;
        }

        pbf_move_left_joystick(context, 128, joystick_y, joystick_ticks, 20);

        context.wait_for_all_requests();
        find_target_bubble_center(env, context, wanted, center_x, center_y);
    }
    
    // camera is roughly looking towards east: walking forward/joystick up means walking eastward
    is_new_position_east = up_ticks > down_ticks;

    // move left or right, no need to be exact as we can just bump into the tables
    uint8_t joystick_x = center_x < 0.5 ? 64 : 192;
    pbf_move_left_joystick(context, joystick_x, 128, 250, 20);
}

std::vector<std::pair<float, float>> AuctionFarmer::detect_sorted_dialog_centers(SingleSwitchProgramEnvironment& env, BotBaseContext& context) {
    context.wait_for_all_requests();
    VideoSnapshot screen = env.console.video().snapshot();
    std::vector<ImageFloatBox> dialog_boxes = detect_dialog_boxes(screen);

    std::vector<std::pair<float, float>> dialog_centers(dialog_boxes.size(), std::pair<float, float>(-1.0f, -1.0f));
    for (size_t i = 0; i < dialog_boxes.size(); i++) {
        get_bubble_center(dialog_boxes[i], dialog_centers[i].first, dialog_centers[i].second);
    }

    // Sort the dilaog centers. We want the top-left dialog to use it as an orientation point.
    // Dialog bubbles are either on the left or right side of the screen, as long as they are on the same side we only care about the y-value.
    // Sort by column (ingame: North-east, north-west, south-east, south-west).
    std::sort(dialog_centers.begin(), dialog_centers.end(), [](std::pair<float, float> left, std::pair<float, float> right) {
        if ((left.first < 0.5 && right.first < 0.5) || (left.first > 0.5 && right.first > 0.5)) {
            return left.second < right.second;
        }
        return left.first < 0.5 && right.first > 0.5;
    });

    return dialog_centers;
}

bool AuctionFarmer::reset_position(SingleSwitchProgramEnvironment& env, BotBaseContext& context, bool move_down) {
    if (ONE_NPC) {
        // No movement, player character should always be directly in front of an npc.
        return false;
    }

    env.log("Resetting position.");
    reset_orientation(env, context, false);
    std::vector<std::pair<float, float>> dialog_centers = detect_sorted_dialog_centers(env, context);

    //OverlayBoxScope optimum_left(env.console.overlay(), ImageFloatBox(OPTIMAL_X - (X_ALPHA / 2.0), 0.0, X_ALPHA, 1.0), COLOR_DARK_BLUE);
    //OverlayBoxScope optimum_right(env.console.overlay(), ImageFloatBox(1 - (OPTIMAL_X - (X_ALPHA / 2.0)), 0.0, X_ALPHA, 1.0), COLOR_DARK_BLUE);
    OverlayBoxScope optimum_left(env.console.overlay(), ImageFloatBox(OPTIMAL_X - (X_ALPHA / 2.0), OPTIMAL_Y - (Y_ALPHA/2.0), X_ALPHA, Y_ALPHA), COLOR_DARK_BLUE);
    OverlayBoxScope optimum_right(env.console.overlay(), ImageFloatBox(1 - (OPTIMAL_X - (X_ALPHA / 2.0)), 1 - (OPTIMAL_Y - (Y_ALPHA / 2.0)), X_ALPHA, Y_ALPHA), COLOR_DARK_BLUE);

    bool did_move = false;
    size_t tries = 0;
    while (dialog_centers.size() < 3 || !is_good_dialog_center(dialog_centers[0].first, dialog_centers[0].second)) {
        env.log("Dialogs  found: " + std::to_string(dialog_centers.size()));
        // Restart in case we lose all orientation points, i.e. dialog bubbles
        if (dialog_centers.empty()) {
            VideoSnapshot screen = env.console.video().snapshot();
            if (tries == 0) {
                AuctionFarmer_Descriptor::Stats& stats = env.current_stats<AuctionFarmer_Descriptor::Stats>();
                stats.m_errors++;
                send_program_recoverable_error_notification(env, NOTIFICATION_ERROR_RECOVERABLE, "Could not detect any offer dialogs. Attempting to recover.", screen);
            }
            tries++;
            if (tries <= 3) {
                env.console.log("Lost all dialog bubbles. Attempting to reset game...");
            }
            else if (tries <= 5) {
                env.console.log("Unable to detect dialog bubbles. Waiting before attempting to reset...");
                context.wait_for(Milliseconds(180000));
            }
            else {
                throw OperationFailedException(
                    ErrorReport::SEND_ERROR_REPORT, env.console,
                    "Could not reset player position.",
                    true
                );
            }
            reset_game(env.program_info(), env.console, context);
            reset_orientation(env, context, false);

            dialog_centers = detect_sorted_dialog_centers(env, context);
            continue;
        }

        float optimal_x = OPTIMAL_X;
        float optimal_y = OPTIMAL_Y;
        float center_x = dialog_centers[0].first;
        float center_y = dialog_centers[0].second;

        OverlayBoxScope center(env.console.overlay(), ImageFloatBox(center_x, center_y, 0.001, 0.001), COLOR_MAGENTA);

        float joystick_modifier_x = 0.0f;
        uint8_t joystick_x = 128;
        if (!is_good_dialog_center(center_x, center_y, false, true)) {
            float diff_left = optimal_x - center_x;
            float diff_right = (1 - optimal_x) - center_x;
            joystick_modifier_x = std::abs(diff_left) < std::abs(diff_right) ? diff_left : diff_right;
            joystick_x = (joystick_modifier_x < 0) ? 192 : 64;
        }
        uint16_t joystick_ticks_x = std::abs(joystick_modifier_x * TICKS_PER_SECOND * 5) + 10; // +10, so there is always some change, but not overshooting the target
        pbf_move_left_joystick(context, joystick_x, 128, joystick_ticks_x, 20);

        
        float joystick_modifier_y = 0.0f;
        uint8_t joystick_y = 128;
        if (!is_good_dialog_center(center_x, center_y, true, false)) {
            float diff_top = optimal_y - center_y;
            joystick_modifier_y = diff_top;
            // TODO: be able to move back once you overshot
            joystick_y = move_down ? 192 : 64;
        }
        uint16_t joystick_ticks_y = std::abs(joystick_modifier_y * TICKS_PER_SECOND * 5);
        pbf_move_left_joystick(context, 128, joystick_y, joystick_ticks_y, 20);


        dialog_centers = detect_sorted_dialog_centers(env, context);
        did_move = true;
    }

    return did_move;
}

bool AuctionFarmer::is_good_dialog_center(float center_x, float center_y, bool ignore_x, bool ignore_y) {
    // Optimal x position does depend on y. 
    // However, since the program can correct later we assume a vertical line, i.e. a y-independent "optimal" x-coordinate
    float optimal_x = OPTIMAL_X;
    float max_distance_x = X_ALPHA;
    float optimal_y = OPTIMAL_Y;
    float max_distance_y = Y_ALPHA;

    bool good_x = std::abs(optimal_x - center_x) <= max_distance_x || std::abs((1 - optimal_x) - center_x) <= max_distance_x || ignore_x;
    bool good_y = std::abs(optimal_y - center_y) <= max_distance_y || std::abs((1 - optimal_y) - center_y) <= max_distance_y || ignore_y;
   
    return good_x && good_y;
}


void AuctionFarmer::reset_orientation(SingleSwitchProgramEnvironment& env, BotBaseContext& context, bool character_orientation) {
    if (character_orientation) {
        open_map_from_overworld(env.program_info(), env.console, context);
        leave_phone_to_overworld(env.program_info(), env.console, context);
        pbf_mash_button(context, BUTTON_L, 20);
    }
    pbf_move_right_joystick(context, 128, 255, 2 * TICKS_PER_SECOND, 20);
}


uint64_t read_next_bid(ConsoleHandle& console, BotBaseContext& context, bool high) {
    float box_y = high ? 0.42f : 0.493f;
    OverlayBoxScope box(console, { 0.73, box_y, 0.17, 0.048 });
    std::unordered_map<uint64_t, size_t> read_bids;
    size_t highest_read = 0;
    uint64_t read_value = 0;

    // read next bid multiple times since the selection arrow sometimes blocks the first digit
    for (size_t i = 0; i < 10; i++) {
        VideoSnapshot screen = console.video().snapshot();
        uint64_t read_bid = OCR::read_number(console.logger(), extract_box_reference(screen, box));

        if (read_bids.find(read_bid) == read_bids.end()) {
            read_bids[read_bid] = 0;
        }
        read_bids[read_bid] += 1;

        if (read_bids[read_bid] > highest_read) {
            highest_read = read_bids[read_bid];
            read_value = read_bid;
        }
        context.wait_for(Milliseconds(20));
    }

    console.log("Next bid: " + std::to_string(read_value));
    return read_value;
}

void AuctionFarmer::bid_on_item(SingleSwitchProgramEnvironment& env, BotBaseContext& context, AuctionOffer offer) {
    AuctionFarmer_Descriptor::Stats& stats = env.current_stats<AuctionFarmer_Descriptor::Stats>();

    VideoSnapshot offer_screen = env.console.video().snapshot();

    AdvanceDialogWatcher advance_detector(COLOR_YELLOW);
    PromptDialogWatcher high_detector(COLOR_RED, { 0.50, 0.40, 0.40, 0.082 });
    PromptDialogWatcher mid_detector(COLOR_PURPLE, { 0.50, 0.475, 0.40, 0.082 });
    PromptDialogWatcher low_detector(COLOR_PURPLE, { 0.50, 0.55, 0.40, 0.082 });
    OverworldWatcher overworld_detector(COLOR_BLUE);
    bool won_auction = true;
    bool auction_ongoing = true;
    int64_t current_bid = 0;

    context.wait_for_all_requests();
    while (auction_ongoing) {
        int ret = wait_until(env.console, context, Milliseconds(5000), { advance_detector, high_detector, mid_detector, low_detector, overworld_detector });
        context.wait_for(Milliseconds(100));

        switch (ret) {
        case 0:
            pbf_press_button(context, BUTTON_A, 20, TICKS_PER_SECOND);
            break;
        case 1:
            current_bid = read_next_bid(env.console, context, true);
            pbf_press_button(context, BUTTON_A, 20, TICKS_PER_SECOND);
            break;
        case 2:
            current_bid = read_next_bid(env.console, context, false);
            pbf_press_button(context, BUTTON_A, 20, TICKS_PER_SECOND);
            break;
        case 3:
            pbf_press_button(context, BUTTON_A, 20, TICKS_PER_SECOND);
            break;
        case 4:
            auction_ongoing = false;
            break;
        default:
            break;
        }
        context.wait_for_all_requests();
    }

    if (won_auction) {
        stats.m_auctions++;
        if (current_bid >= 0) {
            stats.m_money += current_bid;
        }
        env.update_stats();
        send_program_notification(
            env, NOTIFICATION_AUCTION_WIN,
            COLOR_GREEN, "Auction won!",
            {
                { "Item:", get_auction_item_name(offer.item).display_name() },
                { "Final Bid:", std::to_string(current_bid) },
            }
            , "", offer_screen);
    }

    return;
}

void AuctionFarmer::program(SingleSwitchProgramEnvironment& env, BotBaseContext& context){
    assert_16_9_720p_min(env.logger(), env.console);

    AuctionFarmer_Descriptor::Stats& stats = env.current_stats<AuctionFarmer_Descriptor::Stats>();

    //  Connect the controller.
    pbf_press_button(context, BUTTON_LCLICK, 10, 0);
    pbf_wait(context, TICKS_PER_SECOND);
    context.wait_for_all_requests();

    uint8_t year = MAX_YEAR;
    bool current_position_is_east = true;

    while (true) {
        send_program_status_notification(env, NOTIFICATION_STATUS_UPDATE);

        reset_orientation(env, context, true);
        reset_auctions(env, context, true, year, current_position_is_east);
        bool did_move = reset_position(env, context, current_position_is_east);
        if (did_move) {
            reset_orientation(env, context, true);
            context.wait_for_all_requests();
            save_game_from_overworld(env.program_info(), env.console, context);
        }
        
        stats.m_resets++;
        env.update_stats();
        
        bool should_increase_date = false;
        size_t npc_tries = 0;
        std::vector<std::pair<AuctionOffer, ImageFloatBox>> old_offers;
        while (!should_increase_date) {
            if (!ONE_NPC) {
                reset_orientation(env, context, false);
            }

            std::vector<std::pair<AuctionOffer, ImageFloatBox>> offers = check_offers(env, context);
            if (are_offers_equal(old_offers, offers)) {
                should_increase_date = true;
            }
            old_offers = offers;

            for (std::pair<AuctionOffer, ImageFloatBox>& offer_pair : offers){
                AuctionOffer offer = offer_pair.first;
                if (is_good_offer(offer)) {
                    try {
                        move_to_auctioneer(env, context, offer, current_position_is_east);
                    }
                    catch (OperationFailedException& e){
                        stats.m_errors++;

                        npc_tries++;
                        // if ONE_NPC the program already tries multiple times without change to compensate for dropped inputs
                        // at this point it is more likely to be non-recoverable
                        size_t max_npc_tries = ONE_NPC ? 1 : 3;

                        if (npc_tries <= max_npc_tries) {
                            VideoSnapshot screen = env.console.video().snapshot();
                            send_program_recoverable_error_notification(env, NOTIFICATION_ERROR_RECOVERABLE, e.message(), screen);
                        }
                        else {
                            throw OperationFailedException(
                                ErrorReport::SEND_ERROR_REPORT, env.console,
                                "Failed to talk to the NPC!",
                                true
                            );
                        }
                        break;
                    }

                    bid_on_item(env, context, offer);

                    should_increase_date = true;
                    break;
                }
            }
            if (did_move) {
                should_increase_date = true;
            }
            if (!should_increase_date) {
                reset_auctions(env, context, false, year, current_position_is_east);
                stats.m_resets++;
            }

            env.update_stats();
            pbf_wait(context, 125);
            context.wait_for_all_requests();
        }
    }
        
}




}
}
}
