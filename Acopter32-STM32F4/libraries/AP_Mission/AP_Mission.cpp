// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-

/// @file    AP_Mission.cpp
/// @brief   Handles the MAVLINK command mission stack.  Reads and writes mission to storage.

#include "AP_Mission.h"
#include <AP_Terrain.h>

const AP_Param::GroupInfo AP_Mission::var_info[] PROGMEM = {

    // @Param: TOTAL
    // @DisplayName: Total mission commands
    // @Description: The number of mission mission items that has been loaded by the ground station. Do not change this manually.
    // @Range: 0 32766
    // @Increment: 1
    // @User: Advanced
    AP_GROUPINFO("TOTAL",  0, AP_Mission, _cmd_total, 0),

    // @Param: RESTART
    // @DisplayName: Mission Restart when entering Auto mode
    // @Description: Controls mission starting point when entering Auto mode (either restart from beginning of mission or resume from last command run)
    // @Values: 0:Resume Mission, 1:Restart Mission
    AP_GROUPINFO("RESTART",  1, AP_Mission, _restart, AP_MISSION_RESTART_DEFAULT),

    AP_GROUPEND
};

extern const AP_HAL::HAL& hal;

// storage object
StorageAccess AP_Mission::_storage(StorageManager::StorageMission);

///
/// public mission methods
///

/// init - initialises this library including checks the version in eeprom matches this library
void AP_Mission::init()
{
    // check_eeprom_version - checks version of missions stored in eeprom matches this library
    // command list will be cleared if they do not match
    check_eeprom_version();

    // prevent an easy programming error, this will be optimised out
    if (sizeof(union Content) != 12) {
        hal.scheduler->panic(PSTR("AP_Mission Content must be 12 bytes"));
    }

    _last_change_time_ms = hal.scheduler->millis();
}

/// start - resets current commands to point to the beginning of the mission
///     To-Do: should we validate the mission first and return true/false?
void AP_Mission::start()
{
    _flags.state = MISSION_RUNNING;

    reset(); // reset mission to the first command, resets jump tracking
    
    // advance to the first command
    if (!advance_current_nav_cmd()) {
        // on failure set mission complete
        complete();
    }
}

/// stop - stops mission execution.  subsequent calls to update() will have no effect until the mission is started or resumed
void AP_Mission::stop()
{
    _flags.state = MISSION_STOPPED;
}

/// resume - continues the mission execution from where we last left off
///     previous running commands will be re-initialised
void AP_Mission::resume()
{
    // if mission had completed then start it from the first command
    if (_flags.state == MISSION_COMPLETE) {
        start();
        return;
    }

    // if mission had stopped then restart it
    if (_flags.state == MISSION_STOPPED) {
        _flags.state = MISSION_RUNNING;

        // if no valid nav command index restart from beginning
        if (_nav_cmd.index == AP_MISSION_CMD_INDEX_NONE) {
            start();
            return;
        }
    }

    // restart active navigation command. We run these on resume()
    // regardless of whether the mission was stopped, as we may be
    // re-entering AUTO mode and the nav_cmd callback needs to be run
    // to setup the current target waypoint

    // Note: if there is no active command then the mission must have been stopped just after the previous nav command completed
    //      update will take care of finding and starting the nav command
    if (_flags.nav_cmd_loaded) {
        _cmd_start_fn(_nav_cmd);
    }

    // restart active do command
    if (_flags.do_cmd_loaded && _do_cmd.index != AP_MISSION_CMD_INDEX_NONE) {
        _cmd_start_fn(_do_cmd);
    }
}

/// start_or_resume - if MIS_AUTORESTART=0 this will call resume(), otherwise it will call start()
void AP_Mission::start_or_resume()
{
    if (_restart) {
        start();
    } else {
        resume();
    }
}

/// reset - reset mission to the first command
void AP_Mission::reset()
{
    _flags.nav_cmd_loaded  = false;
    _flags.do_cmd_loaded   = false;
    _flags.do_cmd_all_done = false;
    _nav_cmd.index         = AP_MISSION_CMD_INDEX_NONE;
    _do_cmd.index          = AP_MISSION_CMD_INDEX_NONE;
    _prev_nav_cmd_index    = AP_MISSION_CMD_INDEX_NONE;
    init_jump_tracking();
}

/// clear - clears out mission
///     returns true if mission was running so it could not be cleared
bool AP_Mission::clear()
{
    // do not allow clearing the mission while it is running
    if (_flags.state == MISSION_RUNNING) {
        return false;
    }

    // remove all commands
    _cmd_total.set_and_save(0);

    // clear index to commands
    _nav_cmd.index = AP_MISSION_CMD_INDEX_NONE;
    _do_cmd.index = AP_MISSION_CMD_INDEX_NONE;
    _flags.nav_cmd_loaded = false;
    _flags.do_cmd_loaded = false;

    // return success
    return true;
}


/// trucate - truncate any mission items beyond index
void AP_Mission::truncate(uint16_t index)
{
    if ((unsigned)_cmd_total > index) {        
        _cmd_total.set_and_save(index);
    }
}

/// update - ensures the command queues are loaded with the next command and calls main programs command_init and command_verify functions to progress the mission
///     should be called at 10hz or higher
void AP_Mission::update()
{
    // exit immediately if not running or no mission commands
    if (_flags.state != MISSION_RUNNING || _cmd_total == 0) {
        return;
    }

    // check if we have an active nav command
    if (!_flags.nav_cmd_loaded || _nav_cmd.index == AP_MISSION_CMD_INDEX_NONE) {
        // advance in mission if no active nav command
        if (!advance_current_nav_cmd()) {
            // failure to advance nav command means mission has completed
            complete();
            return;
        }
    }else{
        // run the active nav command
        if (_cmd_verify_fn(_nav_cmd)) {
            // market _nav_cmd as complete (it will be started on the next iteration)
            _flags.nav_cmd_loaded = false;
            // immediately advance to the next mission command
            if (!advance_current_nav_cmd()) {
                // failure to advance nav command means mission has completed
                complete();
                return;
            }
        }
    }

    // check if we have an active do command
    if (!_flags.do_cmd_loaded || _do_cmd.index == AP_MISSION_CMD_INDEX_NONE) {
        advance_current_do_cmd();
    }else{
        // run the active do command
        if (_cmd_verify_fn(_do_cmd)) {
            // market _nav_cmd as complete (it will be started on the next iteration)
            _flags.do_cmd_loaded = false;
        }
    }
}

///
/// public command methods
///

/// add_cmd - adds a command to the end of the command list and writes to storage
///     returns true if successfully added, false on failure
///     cmd.index is updated with it's new position in the mission
bool AP_Mission::add_cmd(Mission_Command& cmd)
{
    // attempt to write the command to storage
    bool ret = write_cmd_to_storage(_cmd_total, cmd);

    if (ret) {
        // update command's index
        cmd.index = _cmd_total;
        // increment total number of commands
        _cmd_total.set_and_save(_cmd_total + 1);
    }

    return ret;
}

/// replace_cmd - replaces the command at position 'index' in the command list with the provided cmd
///     replacing the current active command will have no effect until the command is restarted
///     returns true if successfully replaced, false on failure
bool AP_Mission::replace_cmd(uint16_t index, Mission_Command& cmd)
{
    // sanity check index
    if (index >= (unsigned)_cmd_total) {
        return false;
    }

    // attempt to write the command to storage
    return write_cmd_to_storage(index, cmd);
}

/// is_nav_cmd - returns true if the command's id is a "navigation" command, false if "do" or "conditional" command
bool AP_Mission::is_nav_cmd(const Mission_Command& cmd)
{
    return (cmd.id <= MAV_CMD_NAV_LAST);
}

/// get_next_nav_cmd - gets next "navigation" command found at or after start_index
///     returns true if found, false if not found (i.e. reached end of mission command list)
///     accounts for do_jump commands but never increments the jump's num_times_run (advance_current_nav_cmd is responsible for this)
bool AP_Mission::get_next_nav_cmd(uint16_t start_index, Mission_Command& cmd)
{
    uint16_t cmd_index = start_index;

    // search until the end of the mission command list
    while(cmd_index < (unsigned)_cmd_total) {
        // get next command
        if (!get_next_cmd(cmd_index, cmd, false)) {
            // no more commands so return failure
            return false;
        }else{
            // if found a "navigation" command then return it
            if (is_nav_cmd(cmd)) {
                return true;
            }else{
                // move on in list
                cmd_index++;
            }
        }
    }

    // if we got this far we did not find a navigation command
    return false;
}

/// get the ground course of the next navigation leg in centidegrees
/// from 0 36000. Return default_angle if next navigation
/// leg cannot be determined
int32_t AP_Mission::get_next_ground_course_cd(int32_t default_angle)
{
    Mission_Command cmd;
    if (!get_next_nav_cmd(_nav_cmd.index+1, cmd)) {
        return default_angle;
    }
    return get_bearing_cd(_nav_cmd.content.location, cmd.content.location);
}

// set_current_cmd - jumps to command specified by index
bool AP_Mission::set_current_cmd(uint16_t index)
{
    Mission_Command cmd;

    // sanity check index and that we have a mission
    if (index >= (unsigned)_cmd_total || _cmd_total == 1) {
        return false;
    }

    // stop the current running do command
    _do_cmd.index = AP_MISSION_CMD_INDEX_NONE;
    _flags.do_cmd_loaded = false;
    _flags.do_cmd_all_done = false;

    // stop current nav cmd
    _flags.nav_cmd_loaded = false;

    // if index is zero then the user wants to completely restart the mission
    if (index == 0 || _flags.state == MISSION_COMPLETE) {
        _prev_nav_cmd_index = AP_MISSION_CMD_INDEX_NONE;
        // reset the jump tracking to zero
        init_jump_tracking();
        if (index == 0) {
            index = 1;
        }
    }

    // if the mission is stopped or completed move the nav_cmd index to the specified point and set the state to stopped
    // so that if the user resumes the mission it will begin at the specified index
    if (_flags.state != MISSION_RUNNING) {
        // search until we find next nav command or reach end of command list
        while (!_flags.nav_cmd_loaded) {
            // get next command
            if (!get_next_cmd(index, cmd, true)) {
                _nav_cmd.index = AP_MISSION_CMD_INDEX_NONE;
                return false;
            }

            // check if navigation or "do" command
            if (is_nav_cmd(cmd)) {
                // set current navigation command
                _nav_cmd = cmd;
                _flags.nav_cmd_loaded = true;
            }else{
                // set current do command
                if (!_flags.do_cmd_loaded) {
                    _do_cmd = cmd;
                    _flags.do_cmd_loaded = true;
                }
            }
            // move onto next command
            index = cmd.index+1;
        }

        // if we got this far then the mission can safely be "resumed" from the specified index so we set the state to "stopped"
        _flags.state = MISSION_STOPPED;
        return true;
    }

    // the state must be MISSION_RUNNING
    // search until we find next nav command or reach end of command list
    while (!_flags.nav_cmd_loaded) {
        // get next command
        if (!get_next_cmd(index, cmd, true)) {
            // if we run out of nav commands mark mission as complete
            complete();
            // return true because we did what was requested
            // which was apparently to jump to a command at the end of the mission
            return true;
        }

        // check if navigation or "do" command
        if (is_nav_cmd(cmd)) {
            // save previous nav command index
            _prev_nav_cmd_index = _nav_cmd.index;
            // set current navigation command and start it
            _nav_cmd = cmd;
            _flags.nav_cmd_loaded = true;
            _cmd_start_fn(_nav_cmd);
        }else{
            // set current do command and start it (if not already set)
            if (!_flags.do_cmd_loaded) {
                _do_cmd = cmd;
                _flags.do_cmd_loaded = true;
                _cmd_start_fn(_do_cmd);
            }
        }
        // move onto next command
        index = cmd.index+1;
    }

    // if we got this far we must have successfully advanced the nav command
    return true;
}

/// load_cmd_from_storage - load command from storage
///     true is return if successful
bool AP_Mission::read_cmd_from_storage(uint16_t index, Mission_Command& cmd) const
{
    // exit immediately if index is beyond last command but we always let cmd #0 (i.e. home) be read
    if (index > (unsigned)_cmd_total && index != 0) {
        return false;
    }

    // special handling for command #0 which is home
    if (index == 0) {
        cmd.index = 0;
        cmd.id = MAV_CMD_NAV_WAYPOINT;
        cmd.p1 = 0;
        cmd.content.location = _ahrs.get_home();
    }else{
        // Find out proper location in memory by using the start_byte position + the index
        // we can load a command, we don't process it yet
        // read WP position
        uint16_t pos_in_storage = 4 + (index * AP_MISSION_EEPROM_COMMAND_SIZE);

        cmd.id = _storage.read_byte(pos_in_storage);
        cmd.p1 = _storage.read_uint16(pos_in_storage+1);
        _storage.read_block(cmd.content.bytes, pos_in_storage+3, 12);

        // set command's index to it's position in eeprom
        cmd.index = index;
    }

    // return success
    return true;
}

/// write_cmd_to_storage - write a command to storage
///     index is used to calculate the storage location
///     true is returned if successful
bool AP_Mission::write_cmd_to_storage(uint16_t index, Mission_Command& cmd)
{
    // range check cmd's index
    if (index >= num_commands_max()) {
        return false;
    }

    // calculate where in storage the command should be placed
    uint16_t pos_in_storage = 4 + (index * AP_MISSION_EEPROM_COMMAND_SIZE);

    _storage.write_byte(pos_in_storage, cmd.id);
    _storage.write_uint16(pos_in_storage+1, cmd.p1);
    _storage.write_block(pos_in_storage+3, cmd.content.bytes, 12);

    // remember when the mission last changed
    _last_change_time_ms = hal.scheduler->millis();

    // return success
    return true;
}

/// write_home_to_storage - writes the special purpose cmd 0 (home) to storage
///     home is taken directly from ahrs
void AP_Mission::write_home_to_storage()
{
    Mission_Command home_cmd = {};
    home_cmd.id = MAV_CMD_NAV_WAYPOINT;
    home_cmd.content.location = _ahrs.get_home();
    write_cmd_to_storage(0,home_cmd);
}

// mavlink_to_mission_cmd - converts mavlink message to an AP_Mission::Mission_Command object which can be stored to eeprom
//  return true on success, false on failure
bool AP_Mission::mavlink_to_mission_cmd(const mavlink_mission_item_t& packet, AP_Mission::Mission_Command& cmd)
{
    bool copy_location = false;
    bool copy_alt = false;
    uint8_t num_turns, radius_m;    // used by MAV_CMD_NAV_LOITER_TURNS

    // command's position in mission list and mavlink id
    cmd.index = packet.seq;
    cmd.id = packet.command;

    // command specific conversions from mavlink packet to mission command
    switch (cmd.id) {

    case MAV_CMD_NAV_WAYPOINT:                          // MAV ID: 16
        copy_location = true;
        cmd.p1 = packet.param1;                         // delay at waypoint in seconds
        break;

    case MAV_CMD_NAV_LOITER_UNLIM:                      // MAV ID: 17
        copy_location = true;
        cmd.content.location.flags.loiter_ccw = (packet.param3 < 0);    // -1 = counter clockwise, +1 = clockwise
        break;

    case MAV_CMD_NAV_LOITER_TURNS:                      // MAV ID: 18
        copy_location = true;
        num_turns = packet.param1;                      // number of times to circle is held in param1
        radius_m = fabs(packet.param3);                 // radius in meters is held in high in param3
        cmd.p1 = (((uint16_t)radius_m)<<8) | (uint16_t)num_turns;   // store radius in high byte of p1, num turns in low byte of p1
        cmd.content.location.flags.loiter_ccw = (packet.param3 < 0);
        break;

    case MAV_CMD_NAV_LOITER_TIME:                       // MAV ID: 19
        copy_location = true;
        cmd.p1 = packet.param1;                         // loiter time in seconds
        cmd.content.location.flags.loiter_ccw = (packet.param3 < 0);
        break;

    case MAV_CMD_NAV_RETURN_TO_LAUNCH:                  // MAV ID: 20
        copy_location = true;
        break;

    case MAV_CMD_NAV_LAND:                              // MAV ID: 21
        copy_location = true;
        break;

    case MAV_CMD_NAV_TAKEOFF:                           // MAV ID: 22
        copy_location = true;                           // only altitude is used
        cmd.p1 = packet.param1;                         // minimum pitch (plane only)
        break;

    case MAV_CMD_NAV_SPLINE_WAYPOINT:                   // MAV ID: 82
        copy_location = true;
        cmd.p1 = packet.param1;                         // delay at waypoint in seconds
        break;

#ifdef MAV_CMD_NAV_GUIDED
    case MAV_CMD_NAV_GUIDED:                     // MAV ID: 90
        cmd.p1 = packet.param1;                         // max time in seconds the external controller will be allowed to control the vehicle
        cmd.content.nav_guided.alt_min = packet.param2; // min alt below which the command will be aborted.  0 for no lower alt limit
        cmd.content.nav_guided.alt_max = packet.param3; // max alt above which the command will be aborted.  0 for no upper alt limit
        cmd.content.nav_guided.horiz_max = packet.param4;   // max horizontal distance the vehicle can move before the command will be aborted.  0 for no horizontal limit
        break;
#endif

#ifdef MAV_CMD_NAV_VELOCITY
    case MAV_CMD_NAV_VELOCITY:                          // MAV ID: 91
        cmd.p1 = packet.param1;                         // frame - unused
        cmd.content.nav_velocity.x = packet.x;          // lat (i.e. north) velocity in m/s
        cmd.content.nav_velocity.y = packet.y;          // lon (i.e. east) velocity in m/s
        cmd.content.nav_velocity.z = packet.z;          // vertical (i.e. up) velocity in m/s
        break;
#endif

    case MAV_CMD_CONDITION_DELAY:                       // MAV ID: 112
        cmd.content.delay.seconds = packet.param1;      // delay in seconds
        break;

    case MAV_CMD_CONDITION_CHANGE_ALT:                  // MAV ID: 113
        copy_alt = true;                                // only altitude is used
        cmd.content.location.lat = packet.param1 * 100; // climb/descent converted from m/s to cm/s.  To-Do: store in proper climb_rate structure
        break;

    case MAV_CMD_CONDITION_DISTANCE:                    // MAV ID: 114
        cmd.content.distance.meters = packet.param1;    // distance in meters from next waypoint
        break;

    case MAV_CMD_CONDITION_YAW:                         // MAV ID: 115
        cmd.content.yaw.angle_deg = packet.param1;      // target angle in degrees
        cmd.content.yaw.turn_rate_dps = packet.param2;  // 0 = use default turn rate otherwise specific turn rate in deg/sec
        cmd.content.yaw.direction = packet.param3;      // -1 = ccw, +1 = cw
        cmd.content.yaw.relative_angle = packet.param4; // lng=0: absolute angle provided, lng=1: relative angle provided
        break;

    case MAV_CMD_DO_SET_MODE:                           // MAV ID: 176
        cmd.p1 = packet.param1;                         // flight mode identifier
        break;

    case MAV_CMD_DO_JUMP:                               // MAV ID: 177
        cmd.content.jump.target = packet.param1;        // jump-to command number
        cmd.content.jump.num_times = packet.param2;     // repeat count
        break;

    case MAV_CMD_DO_CHANGE_SPEED:                       // MAV ID: 178
        cmd.content.speed.speed_type = packet.param1;   // 0 = airspeed, 1 = ground speed
        cmd.content.speed.target_ms = packet.param2;    // target speed in m/s
        cmd.content.speed.throttle_pct = packet.param3; // throttle as a percentage from 0 ~ 100%
        break;

    case MAV_CMD_DO_SET_HOME:
        copy_location = true;
        cmd.p1 = packet.param1;                         // p1=0 means use current location, p=1 means use provided location
        break;

    case MAV_CMD_DO_SET_PARAMETER:                      // MAV ID: 180
        cmd.p1 = packet.param1;                         // parameter number
        cmd.content.location.alt = packet.param2;       // parameter value
        break;

    case MAV_CMD_DO_SET_RELAY:                          // MAV ID: 181
        cmd.content.relay.num = packet.param1;          // relay number
        cmd.content.relay.state = packet.param2;        // 0:off, 1:on
        break;

    case MAV_CMD_DO_REPEAT_RELAY:                       // MAV ID: 182
        cmd.content.repeat_relay.num = packet.param1;           // relay number
        cmd.content.repeat_relay.repeat_count = packet.param2;  // count
        cmd.content.repeat_relay.cycle_time = packet.param3;    // time converted from seconds to milliseconds
        break;

    case MAV_CMD_DO_SET_SERVO:                          // MAV ID: 183
        cmd.content.servo.channel = packet.param1;      // channel
        cmd.content.servo.pwm = packet.param2;          // PWM
        break;

    case MAV_CMD_DO_REPEAT_SERVO:                       // MAV ID: 184
        cmd.content.repeat_servo.channel = packet.param1;      // channel
        cmd.content.repeat_servo.pwm = packet.param2;          // PWM
        cmd.content.repeat_servo.repeat_count = packet.param3; // count
        cmd.content.repeat_servo.cycle_time = packet.param4;   // time in seconds
        break;

    case MAV_CMD_DO_SET_ROI:                            // MAV ID: 201
        copy_location = true;
        cmd.p1 = packet.param1;                         // 0 = no roi, 1 = next waypoint, 2 = waypoint number, 3 = fixed location, 4 = given target (not supported)
        break;

    case MAV_CMD_DO_DIGICAM_CONTROL:                    // MAV ID: 203
    case MAV_CMD_DO_MOUNT_CONTROL:                      // MAV ID: 205
        // these commands takes no parameters
        break;

    case MAV_CMD_DO_SET_CAM_TRIGG_DIST:                 // MAV ID: 206
        cmd.content.cam_trigg_dist.meters = packet.param1;  // distance between camera shots in meters
        break;

    case MAV_CMD_DO_PARACHUTE:                         // MAV ID: 208
        cmd.p1 = packet.param1;                        // action 0=disable, 1=enable, 2=release.  See PARACHUTE_ACTION enum
        break;

    case MAV_CMD_DO_INVERTED_FLIGHT:                    // MAV ID: 210
        cmd.p1 = packet.param1;                         // normal=0 inverted=1
        break;

    default:
        // unrecognised command
        return false;
    }

    // copy location from mavlink to command
    if (copy_location || copy_alt) {
        switch (packet.frame) {

        case MAV_FRAME_MISSION:
        case MAV_FRAME_GLOBAL:
            if (copy_location) {
                cmd.content.location.lat = 1.0e7f * packet.x;   // floating point latitude to int32_t
                cmd.content.location.lng = 1.0e7f * packet.y;   // floating point longitude to int32_t
            }
            cmd.content.location.alt = packet.z * 100.0f;       // convert packet's alt (m) to cmd alt (cm)
            cmd.content.location.flags.relative_alt = 0;
            break;

        case MAV_FRAME_GLOBAL_RELATIVE_ALT:
            if (copy_location) {
                cmd.content.location.lat = 1.0e7f * packet.x;   // floating point latitude to int32_t
                cmd.content.location.lng = 1.0e7f * packet.y;   // floating point longitude to int32_t
            }
            cmd.content.location.alt = packet.z * 100.0f;       // convert packet's alt (m) to cmd alt (cm)
            cmd.content.location.flags.relative_alt = 1;
            break;

#ifdef MAV_FRAME_LOCAL_NED
        case MAV_FRAME_LOCAL_NED:                         // local (relative to home position)
            if (copy_location) {
                cmd.content.location.lat = 1.0e7f*ToDeg(packet.x/
                                           (RADIUS_OF_EARTH*cosf(ToRad(home.lat/1.0e7f)))) + _ahrs.get_home().lat;
                cmd.content.location.lng = 1.0e7f*ToDeg(packet.y/RADIUS_OF_EARTH) + _ahrs.get_home().lng;
            }
            cmd.content.location.alt = -packet.z*1.0e2f;
            cmd.content.location.flags.relative_alt = 1;
            break;
#endif

#ifdef MAV_FRAME_LOCAL
        case MAV_FRAME_LOCAL:                         // local (relative to home position)
            if (copy_location) {
                cmd.content.location.lat = 1.0e7f*ToDeg(packet.x/
                                           (RADIUS_OF_EARTH*cosf(ToRad(home.lat/1.0e7f)))) + _ahrs.get_home().lat;
                cmd.content.location.lng = 1.0e7f*ToDeg(packet.y/RADIUS_OF_EARTH) + _ahrs.get_home().lng;
            }
            cmd.content.location.alt = packet.z*1.0e2f;
            cmd.content.location.flags.relative_alt = 1;
            break;
#endif

#if AP_TERRAIN_AVAILABLE
        case MAV_FRAME_GLOBAL_TERRAIN_ALT:
            if (copy_location) {
                cmd.content.location.lat = 1.0e7f * packet.x;   // floating point latitude to int32_t
                cmd.content.location.lng = 1.0e7f * packet.y;   // floating point longitude to int32_t
            }
            cmd.content.location.alt = packet.z * 100.0f;       // convert packet's alt (m) to cmd alt (cm)
            // we mark it as a relative altitude, as it doesn't have
            // home alt added
            cmd.content.location.flags.relative_alt = 1;
            // mark altitude as above terrain, not above home
            cmd.content.location.flags.terrain_alt = 1;
            break;
#endif

        default:
            return false;
        }
    }

    // if we got this far then it must have been succesful
    return true;
}

// mission_cmd_to_mavlink - converts an AP_Mission::Mission_Command object to a mavlink message which can be sent to the GCS
//  return true on success, false on failure
bool AP_Mission::mission_cmd_to_mavlink(const AP_Mission::Mission_Command& cmd, mavlink_mission_item_t& packet)
{
    bool copy_location = false;
    bool copy_alt = false;

    // command's position in mission list and mavlink id
    packet.seq = cmd.index;
    packet.command = cmd.id;

    // set defaults
    packet.current = 0;     // 1 if we are passing back the mission command that is currently being executed
    packet.param1 = 0;
    packet.param2 = 0;
    packet.param3 = 0;
    packet.param4 = 0;
    packet.autocontinue = 1;

    // command specific conversions from mission command to mavlink packet
    switch (cmd.id) {

    case MAV_CMD_NAV_WAYPOINT:                          // MAV ID: 16
        copy_location = true;
        packet.param1 = cmd.p1;                         // delay at waypoint in seconds
        break;

    case MAV_CMD_NAV_LOITER_UNLIM:                      // MAV ID: 17
        copy_location = true;
        if (cmd.content.location.flags.loiter_ccw) {
            packet.param3 = -1;
        }else{
            packet.param3 = 1;
        }
        break;

    case MAV_CMD_NAV_LOITER_TURNS:                      // MAV ID: 18
        copy_location = true;
        packet.param1 = LOWBYTE(cmd.p1);                // number of times to circle is held in low byte of p1
        packet.param3 = HIGHBYTE(cmd.p1);               // radius is held in high byte of p1
        if (cmd.content.location.flags.loiter_ccw) {
            packet.param3 = -packet.param3;
        }
        break;

    case MAV_CMD_NAV_LOITER_TIME:                       // MAV ID: 19
        copy_location = true;
        packet.param1 = cmd.p1;                         // loiter time in seconds
        if (cmd.content.location.flags.loiter_ccw) {
            packet.param3 = -1;
        }else{
            packet.param3 = 1;
        }
        break;

    case MAV_CMD_NAV_RETURN_TO_LAUNCH:                  // MAV ID: 20
        copy_location = true;
        break;

    case MAV_CMD_NAV_LAND:                              // MAV ID: 21
        copy_location = true;
        break;

    case MAV_CMD_NAV_TAKEOFF:                           // MAV ID: 22
        copy_location = true;                           // only altitude is used
        packet.param1 = cmd.p1;                         // minimum pitch (plane only)
        break;

    case MAV_CMD_NAV_SPLINE_WAYPOINT:                   // MAV ID: 82
        copy_location = true;
        packet.param1 = cmd.p1;                         // delay at waypoint in seconds
        break;

#ifdef MAV_CMD_NAV_GUIDED
    case MAV_CMD_NAV_GUIDED:                     // MAV ID: 90
        packet.param1 = cmd.p1;                         // max time in seconds the external controller will be allowed to control the vehicle
        packet.param2 = cmd.content.nav_guided.alt_min; // min alt below which the command will be aborted.  0 for no lower alt limit
        packet.param3 = cmd.content.nav_guided.alt_max; // max alt above which the command will be aborted.  0 for no upper alt limit
        packet.param4 = cmd.content.nav_guided.horiz_max;   // max horizontal distance the vehicle can move before the command will be aborted.  0 for no horizontal limit
        break;
#endif

#ifdef MAV_CMD_NAV_VELOCITY
    case MAV_CMD_NAV_VELOCITY:                          // MAV ID: 91
        packet.param1 = cmd.p1;                         // frame - unused
        packet.x = cmd.content.nav_velocity.x;          // lat (i.e. north) velocity in m/s
        packet.y = cmd.content.nav_velocity.y;          // lon (i.e. east) velocity in m/s
        packet.z = cmd.content.nav_velocity.z;          // vertical (i.e. up) velocity in m/s
        break;
#endif

    case MAV_CMD_CONDITION_DELAY:                       // MAV ID: 112
        packet.param1 = cmd.content.delay.seconds;      // delay in seconds
        break;

    case MAV_CMD_CONDITION_CHANGE_ALT:                  // MAV ID: 113
        copy_alt = true;                                // only altitude is used
        packet.param1 = cmd.content.location.alt / 100.0f;  // climb/descent rate converted from cm/s to m/s.  To-Do: store in proper climb_rate structure
        break;

    case MAV_CMD_CONDITION_DISTANCE:                    // MAV ID: 114
        packet.param1 = cmd.content.distance.meters;    // distance in meters from next waypoint
        break;

    case MAV_CMD_CONDITION_YAW:                         // MAV ID: 115
        packet.param1 = cmd.content.yaw.angle_deg;      // target angle in degrees
        packet.param2 = cmd.content.yaw.turn_rate_dps;  // 0 = use default turn rate otherwise specific turn rate in deg/sec
        packet.param3 = cmd.content.yaw.direction;      // -1 = ccw, +1 = cw
        packet.param4 = cmd.content.yaw.relative_angle; // 0 = absolute angle provided, 1 = relative angle provided
        break;

    case MAV_CMD_DO_SET_MODE:                           // MAV ID: 176
        packet.param1 = cmd.p1;                         // set flight mode identifier
        break;

    case MAV_CMD_DO_JUMP:                               // MAV ID: 177
        packet.param1 = cmd.content.jump.target;        // jump-to command number
        packet.param2 = cmd.content.jump.num_times;     // repeat count
        break;

    case MAV_CMD_DO_CHANGE_SPEED:                       // MAV ID: 178
        packet.param1 = cmd.content.speed.speed_type;   // 0 = airspeed, 1 = ground speed
        packet.param2 = cmd.content.speed.target_ms;    // speed in m/s
        packet.param3 = cmd.content.speed.throttle_pct; // throttle as a percentage from 0 ~ 100%
        break;

    case MAV_CMD_DO_SET_HOME:                           // MAV ID: 179
        copy_location = true;
        packet.param1 = cmd.p1;                         // p1=0 means use current location, p=1 means use provided location
        break;

    case MAV_CMD_DO_SET_PARAMETER:                      // MAV ID: 180
        packet.param1 = cmd.p1;                         // parameter number
        packet.param2 = cmd.content.location.alt;       // parameter value
        break;

    case MAV_CMD_DO_SET_RELAY:                          // MAV ID: 181
        packet.param1 = cmd.content.relay.num;          // relay number
        packet.param2 = cmd.content.relay.state;        // 0:off, 1:on
        break;

    case MAV_CMD_DO_REPEAT_RELAY:                       // MAV ID: 182
        packet.param1 = cmd.content.repeat_relay.num;           // relay number
        packet.param2 = cmd.content.repeat_relay.repeat_count;  // count
        packet.param3 = cmd.content.repeat_relay.cycle_time;    // time in seconds
        break;

    case MAV_CMD_DO_SET_SERVO:                          // MAV ID: 183
        packet.param1 = cmd.content.servo.channel;      // channel
        packet.param2 = cmd.content.servo.pwm;          // PWM
        break;

    case MAV_CMD_DO_REPEAT_SERVO:                       // MAV ID: 184
        packet.param1 = cmd.content.repeat_servo.channel;       // channel
        packet.param2 = cmd.content.repeat_servo.pwm;           // PWM
        packet.param3 = cmd.content.repeat_servo.repeat_count;  // count
        packet.param4 = cmd.content.repeat_servo.cycle_time;    // time in milliseconds converted to seconds
        break;

    case MAV_CMD_DO_SET_ROI:                            // MAV ID: 201
        copy_location = true;
        packet.param1 = cmd.p1;                         // 0 = no roi, 1 = next waypoint, 2 = waypoint number, 3 = fixed location, 4 = given target (not supported)
        break;

    case MAV_CMD_DO_DIGICAM_CONTROL:                    // MAV ID: 203
    case MAV_CMD_DO_MOUNT_CONTROL:                      // MAV ID: 205
        // these commands takes no parameters
        break;

    case MAV_CMD_DO_SET_CAM_TRIGG_DIST:                 // MAV ID: 206
        packet.param1 = cmd.content.cam_trigg_dist.meters;  // distance between camera shots in meters
        break;

    case MAV_CMD_DO_PARACHUTE:                          // MAV ID: 208
        packet.param1 = cmd.p1;                         // action 0=disable, 1=enable, 2=release.  See PARACHUTE_ACTION enum
        break;

    case MAV_CMD_DO_INVERTED_FLIGHT:                    // MAV ID: 210
        packet.param1 = cmd.p1;                         // normal=0 inverted=1
        break;

    default:
        // unrecognised command
        return false;
    }

    // copy location from mavlink to command
    if (copy_location) {
        packet.x = cmd.content.location.lat / 1.0e7f;   // int32_t latitude to float
        packet.y = cmd.content.location.lng / 1.0e7f;   // int32_t longitude to float
    }
    if (copy_location || copy_alt) {
        packet.z = cmd.content.location.alt / 100.0f;   // cmd alt in cm to m
        if (cmd.content.location.flags.relative_alt) {
            packet.frame = MAV_FRAME_GLOBAL_RELATIVE_ALT;
        }else{
            packet.frame = MAV_FRAME_GLOBAL;
        }
#if AP_TERRAIN_AVAILABLE
        if (cmd.content.location.flags.terrain_alt) {
            // this is a above-terrain altitude
            if (!cmd.content.location.flags.relative_alt) {
                // refuse to return non-relative terrain mission
                // items. Internally we do have these, and they
                // have home.alt added, but we should never be
                // returning them to the GCS, as the GCS doesn't know
                // our home.alt, so it would have no way to properly
                // interpret it
                return false;
            }
            packet.z = cmd.content.location.alt * 0.01f;
            packet.frame = MAV_FRAME_GLOBAL_TERRAIN_ALT;
        }
#else
        // don't ever return terrain mission items if no terrain support
        if (cmd.content.location.flags.terrain_alt) {
            return false;
        }
#endif
    }

    // if we got this far then it must have been successful
    return true;
}

///
/// private methods
///

/// complete - mission is marked complete and clean-up performed including calling the mission_complete_fn
void AP_Mission::complete()
{
    // flag mission as complete
    _flags.state = MISSION_COMPLETE;

    // callback to main program's mission complete function
    _mission_complete_fn();
}

/// advance_current_nav_cmd - moves current nav command forward
///     do command will also be loaded
///     accounts for do-jump commands
//      returns true if command is advanced, false if failed (i.e. mission completed)
bool AP_Mission::advance_current_nav_cmd()
{
    Mission_Command cmd;
    uint16_t cmd_index;

    // exit immediately if we're not running
    if (_flags.state != MISSION_RUNNING) {
        return false;
    }

    // exit immediately if current nav command has not completed
    if (_flags.nav_cmd_loaded) {
        return false;
    }

    // stop the current running do command
    _do_cmd.index = AP_MISSION_CMD_INDEX_NONE;
    _flags.do_cmd_loaded = false;
    _flags.do_cmd_all_done = false;

    // get starting point for search
    cmd_index = _nav_cmd.index;
    if (cmd_index == AP_MISSION_CMD_INDEX_NONE) {
        // start from beginning of the mission command list
        cmd_index = AP_MISSION_FIRST_REAL_COMMAND;
    }else{
        // start from one position past the current nav command
        cmd_index++;
    }

    // search until we find next nav command or reach end of command list
    uint8_t max_loops = 64;
    while (!_flags.nav_cmd_loaded && --max_loops) {
        // get next command
        if (!get_next_cmd(cmd_index, cmd, true)) {
            return false;
        }

        // check if navigation or "do" command
        if (is_nav_cmd(cmd)) {
            // save previous nav command index
            _prev_nav_cmd_index = _nav_cmd.index;
            // set current navigation command and start it
            _nav_cmd = cmd;
            _flags.nav_cmd_loaded = true;
            _cmd_start_fn(_nav_cmd);
        }else{
            // set current do command and start it (if not already set)
            if (!_flags.do_cmd_loaded) {
                _do_cmd = cmd;
                _flags.do_cmd_loaded = true;
                _cmd_start_fn(_do_cmd);
            }
        }
        // move onto next command
        cmd_index = cmd.index+1;
    }

    if (max_loops == 0) {
        // the mission is looping
        return false;
    }

    // if we got this far we must have successfully advanced the nav command
    return true;
}

/// advance_current_do_cmd - moves current do command forward
///     accounts for do-jump commands
void AP_Mission::advance_current_do_cmd()
{
    Mission_Command cmd;
    uint16_t cmd_index;

    // exit immediately if we're not running or we've completed all possible "do" commands
    if (_flags.state != MISSION_RUNNING || _flags.do_cmd_all_done) {
        return;
    }

    // get starting point for search
    cmd_index = _do_cmd.index;
    if (cmd_index == AP_MISSION_CMD_INDEX_NONE) {
        cmd_index = AP_MISSION_FIRST_REAL_COMMAND;
    }else{
        // start from one position past the current do command
        cmd_index++;
    }

    // check if we've reached end of mission
    if (cmd_index >= (unsigned)_cmd_total) {
        // set flag to stop unnecessarily searching for do commands
        _flags.do_cmd_all_done = true;
        return;
    }

    // find next do command
    if (get_next_do_cmd(cmd_index, cmd)) {
        // set current do command and start it
        _do_cmd = cmd;
        _flags.do_cmd_loaded = true;
        _cmd_start_fn(_do_cmd);
    }else{
        // set flag to stop unnecessarily searching for do commands
        _flags.do_cmd_all_done = true;
    }
}

/// get_next_cmd - gets next command found at or after start_index
///     returns true if found, false if not found (i.e. mission complete)
///     accounts for do_jump commands
///     increment_jump_num_times_if_found should be set to true if advancing the active navigation command
bool AP_Mission::get_next_cmd(uint16_t start_index, Mission_Command& cmd, bool increment_jump_num_times_if_found)
{
    uint16_t cmd_index = start_index;
    Mission_Command temp_cmd;
    uint16_t jump_index = AP_MISSION_CMD_INDEX_NONE;

    // search until the end of the mission command list
    uint8_t max_loops = 64;
    while(cmd_index < (unsigned)_cmd_total) {
        // load the next command
        if (!read_cmd_from_storage(cmd_index, temp_cmd)) {
            // this should never happen because of check above but just in case
            return false;
        }

        // check for do-jump command
        if (temp_cmd.id == MAV_CMD_DO_JUMP) {

            if (max_loops-- == 0) {
                return false;
            }

            // check for invalid target
            if (temp_cmd.content.jump.target >= (unsigned)_cmd_total) {
                // To-Do: log an error?
                return false;
            }

            // check for endless loops
            if (!increment_jump_num_times_if_found && jump_index == cmd_index) {
                // we have somehow reached this jump command twice and there is no chance it will complete
                // To-Do: log an error?
                return false;
            }

            // record this command so we can check for endless loops
            if (jump_index == AP_MISSION_CMD_INDEX_NONE) {
                jump_index = cmd_index;
            }

            // check if jump command is 'repeat forever'
            if (temp_cmd.content.jump.num_times == AP_MISSION_JUMP_REPEAT_FOREVER) {
                // continue searching from jump target
                cmd_index = temp_cmd.content.jump.target;
            }else{
                // get number of times jump command has already been run
                int16_t jump_times_run = get_jump_times_run(temp_cmd);
                if (jump_times_run < temp_cmd.content.jump.num_times) {
                    // update the record of the number of times run
                    if (increment_jump_num_times_if_found) {
                        increment_jump_times_run(temp_cmd);
                    }
                    // continue searching from jump target
                    cmd_index = temp_cmd.content.jump.target;
                }else{
                    // jump has been run specified number of times so move search to next command in mission
                    cmd_index++;
                }
            }
        }else{
            // this is a non-jump command so return it
            cmd = temp_cmd;
            return true;
        }
    }

    // if we got this far we did not find a navigation command
    return false;
}

/// get_next_do_cmd - gets next "do" or "conditional" command after start_index
///     returns true if found, false if not found
///     stops and returns false if it hits another navigation command before it finds the first do or conditional command
///     accounts for do_jump commands but never increments the jump's num_times_run (advance_current_nav_cmd is responsible for this)
bool AP_Mission::get_next_do_cmd(uint16_t start_index, Mission_Command& cmd)
{
    Mission_Command temp_cmd;

    // check we have not passed the end of the mission list
    if (start_index >= (unsigned)_cmd_total) {
        return false;
    }

    // get next command
    if (!get_next_cmd(start_index, temp_cmd, false)) {
        // no more commands so return failure
        return false;
    }else if (is_nav_cmd(temp_cmd)) {
        // if it's a "navigation" command then return false because we do not progress past nav commands
        return false;
    }else{
        // this must be a "do" or "conditional" and is not a do-jump command so return it
        cmd = temp_cmd;
        return true;
    }
}

///
/// jump handling methods
///

// init_jump_tracking - initialise jump_tracking variables
void AP_Mission::init_jump_tracking()
{
    for(uint8_t i=0; i<AP_MISSION_MAX_NUM_DO_JUMP_COMMANDS; i++) {
        _jump_tracking[i].index = AP_MISSION_CMD_INDEX_NONE;
        _jump_tracking[i].num_times_run = 0;
    }
}

/// get_jump_times_run - returns number of times the jump command has been run
int16_t AP_Mission::get_jump_times_run(const Mission_Command& cmd)
{
    // exit immediatley if cmd is not a do-jump command or target is invalid
    if (cmd.id != MAV_CMD_DO_JUMP || cmd.content.jump.target >= (unsigned)_cmd_total) {
        // To-Do: log an error?
        return AP_MISSION_JUMP_TIMES_MAX;
    }

    // search through jump_tracking array for this cmd
    for (uint8_t i=0; i<AP_MISSION_MAX_NUM_DO_JUMP_COMMANDS; i++) {
        if (_jump_tracking[i].index == cmd.index) {
            return _jump_tracking[i].num_times_run;
        }else if(_jump_tracking[i].index == AP_MISSION_CMD_INDEX_NONE) {
            // we've searched through all known jump commands and haven't found it so allocate new space in _jump_tracking array
            _jump_tracking[i].index = cmd.index;
            _jump_tracking[i].num_times_run = 0;
            return 0;
        }
    }

    // if we've gotten this far then the _jump_tracking array must be full
    // To-Do: log an error?
    return AP_MISSION_JUMP_TIMES_MAX;
}

/// increment_jump_times_run - increments the recorded number of times the jump command has been run
void AP_Mission::increment_jump_times_run(Mission_Command& cmd)
{
    // exit immediately if cmd is not a do-jump command
    if (cmd.id != MAV_CMD_DO_JUMP) {
        // To-Do: log an error?
        return;
    }

    // search through jump_tracking array for this cmd
    for (uint8_t i=0; i<AP_MISSION_MAX_NUM_DO_JUMP_COMMANDS; i++) {
        if (_jump_tracking[i].index == cmd.index) {
            _jump_tracking[i].num_times_run++;
            return;
        }else if(_jump_tracking[i].index == AP_MISSION_CMD_INDEX_NONE) {
            // we've searched through all known jump commands and haven't found it so allocate new space in _jump_tracking array
            _jump_tracking[i].index = cmd.index;
            _jump_tracking[i].num_times_run = 1;
            return;
        }
    }

    // if we've gotten this far then the _jump_tracking array must be full
    // To-Do: log an error
    return;
}

// check_eeprom_version - checks version of missions stored in eeprom matches this library
// command list will be cleared if they do not match
void AP_Mission::check_eeprom_version()
{
    uint32_t eeprom_version = _storage.read_uint32(0);

    // if eeprom version does not match, clear the command list and update the eeprom version
    if (eeprom_version != AP_MISSION_EEPROM_VERSION) {
        if (clear()) {
            _storage.write_uint32(0, AP_MISSION_EEPROM_VERSION);
        }
    }
}

/*
  return total number of commands that can fit in storage space
 */
uint16_t AP_Mission::num_commands_max(void) const
{
    // -4 to remove space for eeprom version number
    return (_storage.size() - 4) / AP_MISSION_EEPROM_COMMAND_SIZE;
}

