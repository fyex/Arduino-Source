/*  RNG Manipulation of the Highlight Watt Trader in the Snowslide Slope area in the Crown Tundra
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 *  Based on Anubis' findings: https://docs.google.com/spreadsheets/u/0/d/1pNYtCJKRh_efX9LvzjCiA-0n2lGSFnVmSWwmPzgSOMw/htmlview
 *
 */

#include <algorithm>
#include <set>
#include "CommonFramework/Exceptions/ProgramFinishedException.h"
#include "CommonFramework/Exceptions/OperationFailedException.h"
#include "CommonFramework/Notifications/ProgramNotifications.h"
#include "CommonFramework/VideoPipeline/VideoFeed.h"
#include "CommonFramework/Tools/StatsTracking.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "Pokemon/Pokemon_Strings.h"
#include "PokemonSwSh/PokemonSwSh_Settings.h"
#include "PokemonSwSh/Commands/PokemonSwSh_Commands_DateSpam.h"
#include "PokemonSwSh/Inference/PokemonSwSh_SelectionArrowFinder.h"
#include "PokemonSwSh/Programs/PokemonSwSh_MenuNavigation.h"
#include "PokemonSwSh/Programs/RNG/PokemonSwSh_BasicRNG.h"
#include "PokemonSwSh/Programs/RNG/PokemonSwSh_DailyHighlightRNG.h"


namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonSwSh{
using namespace Pokemon;


const DailyHighlightDatabase& DAILY_HIGHLIGHT_DATABASE() {
    static DailyHighlightDatabase database("PokemonSwSh/DailyHighlights.json");
    return database;
}


DailyHighlightRNG_Descriptor::DailyHighlightRNG_Descriptor()
    : SingleSwitchProgramDescriptor(
        "PokemonSwSh:DailyHighlightRNG",
        STRING_POKEMON + " SwSh", "Daily Highlight RNG",
        "ComputerControl/blob/master/Wiki/Programs/PokemonSwSh/DailyHighlightRNG.md",
        "Perform RNG manipulation to get rare items from the daily highlight trader.",
        FeedbackType::REQUIRED,
        AllowCommandsWhenRunning::DISABLE_COMMANDS,
        PABotBaseLevel::PABOTBASE_12KB
    )
{}

struct DailyHighlightRNG_Descriptor::Stats : public StatsTracker{
public:
    Stats()
        : iterations(m_stats["Iterations"])
        , reads(m_stats["Seed Reads"])
        , errors(m_stats["Errors"])
        , highlights(m_stats["Highlights"])
    {
        m_display_order.emplace_back("Iterations");
        m_display_order.emplace_back("Seed Reads");
        m_display_order.emplace_back("Highlights");
        m_display_order.emplace_back("Errors", HIDDEN_IF_ZERO);
    }

public:
    std::atomic<uint64_t>& iterations;
    std::atomic<uint64_t>& reads;
    std::atomic<uint64_t>& errors;
    std::atomic<uint64_t>& highlights;
};
std::unique_ptr<StatsTracker> DailyHighlightRNG_Descriptor::make_stats() const{
    return std::unique_ptr<StatsTracker>(new Stats());
}

DailyHighlightRNG::DailyHighlightRNG()
    : NUM_HIGHLIGHTS(
        "<b>Number of highlights:</b><br>How many daily highlights should be bought. A value of 0 will run until you run out of Watts.",
        LockMode::UNLOCK_WHILE_RUNNING, 0)
    , CONTINUE(
        "<b>Continue from last time:</b><br>If the initial two daily highlights are already manipulated and should be bought.",
        LockMode::LOCK_WHILE_RUNNING, false)
    , HIGHLIGHT_SELECTION(
        "<b>Desired Highlights:</b>", 
        "Highlight", 
        DAILY_HIGHLIGHT_DATABASE().database(), 
        "bottle-cap-1")
    , NOTIFICATION_STATUS_UPDATE("Status Update", true, false, std::chrono::seconds(3600))
    , NOTIFICATIONS({
        &NOTIFICATION_STATUS_UPDATE,
        &NOTIFICATION_PROGRAM_FINISH,
        &NOTIFICATION_ERROR_RECOVERABLE,
        &NOTIFICATION_ERROR_FATAL,
    })
    , m_advanced_options(
        "<font size=4><b>Advanced Options:</b> You should not need to touch anything below here.</font>"
    )
    , MAX_UNKNOWN_ADVANCES(
        "<b>Max Unknown advances:</b><br>How many advances to check when updating the rng state.",
        LockMode::LOCK_WHILE_RUNNING,
        100000
    )
    , ADVANCE_PRESS_DURATION(
        "<b>Advance Press Duration:</b><br>Hold the button down for this long to advance once.",
        LockMode::LOCK_WHILE_RUNNING,
        10
    )
    , ADVANCE_RELEASE_DURATION(
        "<b>Advance Release Duration:</b><br>After releasing the button, wait this long before pressing it again.",
        LockMode::LOCK_WHILE_RUNNING,
        10
    )
    , SAVE_SCREENSHOTS(
        "<b>Save Debug Screenshots:</b>",
        LockMode::LOCK_WHILE_RUNNING,
        false
    )
    , LOG_VALUES(
        "<b>Log Animation Values:</br>",
        LockMode::LOCK_WHILE_RUNNING,
        false
    )
{
    PA_ADD_OPTION(START_LOCATION);

    PA_ADD_OPTION(NUM_HIGHLIGHTS);
    PA_ADD_OPTION(CONTINUE);
    PA_ADD_OPTION(HIGHLIGHT_SELECTION);

    PA_ADD_OPTION(NOTIFICATIONS);

    PA_ADD_STATIC(m_advanced_options);
    PA_ADD_OPTION(MAX_UNKNOWN_ADVANCES);
    PA_ADD_OPTION(ADVANCE_PRESS_DURATION);
    PA_ADD_OPTION(ADVANCE_RELEASE_DURATION);
    PA_ADD_OPTION(SAVE_SCREENSHOTS);
    PA_ADD_OPTION(LOG_VALUES);
}

void DailyHighlightRNG::move_to_trader(SingleSwitchProgramEnvironment& env, BotBaseContext& context) {
    pbf_move_left_joystick(context, 207, 1, 160, 10); // Magic numbers to barely reach the trader
    pbf_press_button(context, BUTTON_A, 20, 20);
    // TODO: check if NPC was reached -> DialogArrow
}

void DailyHighlightRNG::navigate_to_party(SingleSwitchProgramEnvironment& env, BotBaseContext& context){
    pbf_mash_button(context, BUTTON_B, 2 * TICKS_PER_SECOND); // exit dialog
    pbf_press_button(context, BUTTON_X, 10, GameSettings::instance().OVERWORLD_TO_MENU_DELAY);
    navigate_to_menu_app(env, env.console, context, 1, NOTIFICATION_ERROR_RECOVERABLE); // TODO: try-catch-block + error recovery
    pbf_press_button(context, BUTTON_A, 10, 3*TICKS_PER_SECOND);
    context.wait_for_all_requests();
}

uint8_t DailyHighlightRNG::calibrate_num_npc_from_party(SingleSwitchProgramEnvironment& env, BotBaseContext& context, Pokemon::Xoroshiro128Plus& rng){
    // TODO: implement
    return 2; // Usually either 1 or 2 -> higher numbers suggest bad npc state
}

size_t DailyHighlightRNG::calculate_target(SingleSwitchProgramEnvironment& env, Xoroshiro128PlusState state, uint8_t num_npcs, std::vector<std::string> wanted_highlights){
    Xoroshiro128Plus rng(state);
    size_t advances = 0;
    bool found_advance_amount = false;

    std::vector<std::pair<uint16_t, uint16_t>> ranges; 
    for (std::string slug : wanted_highlights) {
        ranges.push_back(DAILY_HIGHLIGHT_DATABASE().get_range_for_slug(slug));
    }

    while (!found_advance_amount) {
        // calculate the result for the current temp_rng state
        Xoroshiro128Plus temp_rng(rng.get_state());

        for (size_t i = 0; i < num_npcs; i++) {
            temp_rng.nextInt(91);
        }
        temp_rng.next();
        temp_rng.nextInt(60);

        uint64_t highlight_roll = temp_rng.nextInt(1000);

        for (auto& range : ranges) {
            if (range.first < highlight_roll && range.second > highlight_roll) {
                found_advance_amount = true;
                // TODO: check if affordable, remove from vector if not. If vector empty: throw Exception
            }
        }
        
        if (!found_advance_amount) {
            rng.next();
            advances++;
        }
    }

    return advances;
}

void DailyHighlightRNG::leave_to_overworld_and_interact(SingleSwitchProgramEnvironment& env, BotBaseContext& context, bool buy_highlight) {
    // Close menu
    pbf_press_button(context, BUTTON_B, 2 * TICKS_PER_SECOND, 5);
    pbf_press_button(context, BUTTON_B, 10, 70);

    // Quickly interact
    pbf_press_button(context, BUTTON_A, 30, 30);
    pbf_wait(context, 2*TICKS_PER_SECOND);

    // TODO: check if interaction worked -> see move_to_trader()

    // Buy highlight
    if (buy_highlight) {
        pbf_press_button(context, BUTTON_ZL, 10, 40);
        pbf_press_dpad(context, DPAD_DOWN, 10, 10);
        pbf_mash_button(context, BUTTON_ZL, 400);
    }

    // Leave dialog
    pbf_mash_button(context, BUTTON_B, 6 * TICKS_PER_SECOND);
}

void DailyHighlightRNG::recover_from_wrong_state(SingleSwitchProgramEnvironment& env, BotBaseContext& context) {
    // Mash the B button to exit potential menus or dialog boxes
    pbf_mash_button(context, BUTTON_B, 30 * TICKS_PER_SECOND);
    
    // Open map
    pbf_press_button(context, BUTTON_X, 20, GameSettings::instance().OVERWORLD_TO_MENU_DELAY);
    navigate_to_menu_app(env, env.console, context, 5, NOTIFICATION_ERROR_RECOVERABLE);

    // Fly to Snowslide Slope
    pbf_move_left_joystick(context, 200, 210, 20, 20);
    pbf_mash_button(context, BUTTON_A, 1 * TICKS_PER_SECOND);
}


void DailyHighlightRNG::advance_date(SingleSwitchProgramEnvironment& env, BotBaseContext& context, uint8_t& year) {
    pbf_press_button(context, BUTTON_HOME, 10, GameSettings::instance().GAME_TO_HOME_DELAY_FAST);
    home_roll_date_enter_game_autorollback(env.console, context, year);
    //resume_game_from_home(env.console, context, false);
}


void DailyHighlightRNG::program(SingleSwitchProgramEnvironment& env, BotBaseContext& context){
    DailyHighlightRNG_Descriptor::Stats& stats = env.current_stats<DailyHighlightRNG_Descriptor::Stats>();
    env.update_stats();

    std::vector<std::string> wanted_highlights = HIGHLIGHT_SELECTION.all_slugs();
    if (wanted_highlights.empty()){
        throw UserSetupError(env.console, "At least one highlight item needs to be selected!");
    }

    if (START_LOCATION.start_in_grip_menu()) {
        grip_menu_connect_go_home(context);
    }
    else {
        pbf_press_button(context, BUTTON_B, 5, 5);
    }

    Xoroshiro128Plus rng(0, 0);
    bool is_state_valid = false;
    size_t iteration = 0;
    size_t bought_highlights = 0; // = CONTINUE ? iteration : max(0, iteration - 2);
    uint8_t year = MAX_YEAR;
    uint16_t state_errors = 0;

    // TODO: save from time to time
    move_to_trader(env, context);

    while (bought_highlights < NUM_HIGHLIGHTS || NUM_HIGHLIGHTS <= 0){
        iteration++;
        stats.iterations++;
        env.update_stats();
        send_program_status_notification(env, NOTIFICATION_STATUS_UPDATE);
        env.console.log("Daily Highlight RNG iteration: " + std::to_string(iteration));
        
        advance_date(env, context, year);
        navigate_to_party(env, context);
        context.wait_for_all_requests();

        // Find RNG state
        if (!is_state_valid){
            rng = Xoroshiro128Plus(find_rng_state(env.console, context, SAVE_SCREENSHOTS, LOG_VALUES));
//            rng = Xoroshiro128Plus(100, 10000);
            is_state_valid = true;
            stats.reads++;
        }else{
            rng = Xoroshiro128Plus(refind_rng_state(env.console, context, rng.get_state(), 0, MAX_UNKNOWN_ADVANCES, SAVE_SCREENSHOTS, LOG_VALUES));
            stats.reads++;
        }

        Xoroshiro128PlusState rng_state = rng.get_state();
        if (rng_state.s0 == 0 && rng_state.s1 == 0){
            stats.errors++;
            env.update_stats();

            state_errors++;
            if (state_errors >= 3){
                throw OperationFailedException(
                    ErrorReport::SEND_ERROR_REPORT, env.console,
                    "Detected invalid RNG state three times in a row."
                );
            }
            VideoSnapshot screen = env.console.video().snapshot();
            send_program_recoverable_error_notification(env, NOTIFICATION_ERROR_RECOVERABLE, "Detected invalid RNG state.", screen);
            recover_from_wrong_state(env, context);
            is_state_valid = false;
            continue;
        }

        // Calibrate number of NPCs in the area and check whether Trader is in slow state
        uint8_t num_npcs = calibrate_num_npc_from_party(env, context, rng);

        // Do advances
        size_t target_advances = calculate_target(env, rng.get_state(), num_npcs, HIGHLIGHT_SELECTION.all_slugs());
        env.console.log("Needed advances: " + std::to_string(target_advances));
        do_rng_advances(env.console, context, rng, target_advances, ADVANCE_PRESS_DURATION, ADVANCE_RELEASE_DURATION);

        // Talk to NPC and buy highlight
        bool buy_highlight = (iteration >= 3) || CONTINUE;
        leave_to_overworld_and_interact(env, context, buy_highlight);

        if (buy_highlight){
            bought_highlights++;
            stats.highlights++;
        }        

        uint32_t watts = 1000000; // TODO: read screen
        uint32_t lowest_cost = 500; // TODO: calculate
        // Out of Watts.
        if (watts < lowest_cost){
            throw ProgramFinishedException(env.console, "Cannot buy more daily highlights with the remaining Watts.");
        }
        
        env.update_stats();
        state_errors = 0;
    }
    send_program_finished_notification(env, NOTIFICATION_PROGRAM_FINISH);
}


}
}
}
