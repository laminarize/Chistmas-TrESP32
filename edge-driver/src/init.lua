-- Copyright 2026 Pixel Paw Studio
-- Licensed under the Apache License, Version 2.0
--
-- Local SmartThings Matter Edge Driver for Christmas TrESP32.
-- Exposes ONE single entry in SmartThings ("Christmas TrESP32") backed by
-- ONE Matter endpoint (Endpoint 1) with:
--   1. main:    Master Power On/Off + Dimmer Slider (1% - 100%)
--   2. rgbMode: Simple "RGB Multicolor" On/Off Toggle Switch
--               (OFF = Warm White / Polarity A, ON = RGB Multicolor / Polarity B)
--   3. Full support for preferred dim levels in BOTH SmartThings Routines
--      (Turn On + Dimmer % and Turn Off + pre-staged Dimmer %) AND Device
--      Settings preferences for the card's built-in Power-On / Power-Off schedules.

local MatterDriver = require "st.matter.driver"
local capabilities = require "st.capabilities"
local clusters = require "st.matter.clusters"
local st_utils = require "st.utils"
local log = require "log"

-- Endpoint 1 ColorTemperatureMireds encoding on Christmas TrESP32:
--   154 mireds (<= 300) -> RGB Multicolor ON  (Polarity B / -29V)
--   455 mireds (>  300) -> Warm White (RGB OFF, Polarity A / +29V)
local MIREDS_RGB_ON = 154
local MIREDS_WARM_WHITE = 455
local MIREDS_RGB_THRESHOLD = 300

local ZERO_TRANSITION_TIME = 0
local DEFAULT_STEP_TRANSITION_TIME = 3
local OPTIONS_MASK = 0x01
local HANDLE_COMMAND_IF_OFF = 0x01
local IGNORE_COMMAND_IF_OFF = 0x00

-- Transient timestamps (seconds) to coordinate simultaneous Routine commands
-- (e.g. "Turn Off + Dimmer 30%" or "Turn On + Dimmer 60%"):
local FIELD_LAST_OFF_CMD_TS = "__last_off_cmd_ts"
local FIELD_LAST_LEVEL_CMD_TS = "__last_level_cmd_ts"
local ROUTINE_COALESCE_WINDOW_SEC = 2

local subscribed_attributes = {
  [capabilities.switch.ID] = {
    clusters.OnOff.attributes.OnOff,
    clusters.ColorControl.attributes.ColorTemperatureMireds,
  },
  [capabilities.switchLevel.ID] = {
    clusters.LevelControl.attributes.CurrentLevel,
  },
}

local function find_light_endpoint(device)
  local eps = device:get_endpoints(clusters.OnOff.ID) or {}
  table.sort(eps)
  for _, ep in ipairs(eps) do
    if ep ~= 0 then
      return ep
    end
  end
  return device.MATTER_DEFAULT_ENDPOINT or 1
end

local function component_to_endpoint(device, component_id)
  return find_light_endpoint(device)
end

local function pct_to_matter_level(pct)
  return st_utils.clamp_value(st_utils.round((pct / 100.0) * 254.0), 1, 254)
end

local function refresh_state(device)
  local ep = find_light_endpoint(device)
  device:send(clusters.OnOff.attributes.OnOff:read(device, ep))
  device:send(clusters.LevelControl.attributes.CurrentLevel:read(device, ep))
  device:send(clusters.ColorControl.attributes.ColorTemperatureMireds:read(device, ep))
end

local function ensure_subscriptions(device)
  for cap_id, attributes in pairs(subscribed_attributes) do
    if device:supports_capability_by_id(cap_id) then
      for _, attr in ipairs(attributes) do
        device:add_subscribed_attribute(attr)
      end
    end
  end
  device:subscribe()
end

-- [[ LIFECYCLE HANDLERS ]] --

local function device_init(driver, device)
  device:set_component_to_endpoint_fn(component_to_endpoint)
  ensure_subscriptions(device)
end

local function device_added(driver, device)
  device:set_component_to_endpoint_fn(component_to_endpoint)
  local rgb_comp = device.profile.components.rgbMode
  if rgb_comp ~= nil then
    local current_rgb = device:get_latest_state("rgbMode", capabilities.switch.ID, capabilities.switch.switch.NAME)
    if current_rgb == nil then
      device:emit_component_event(rgb_comp, capabilities.switch.switch.off())
    end
  end
  refresh_state(device)
end

local function do_configure(driver, device)
  device:set_component_to_endpoint_fn(component_to_endpoint)
  device:try_update_metadata({ profile = "christmas-tresp32", provisioning_state = "PROVISIONED" })
  ensure_subscriptions(device)
  refresh_state(device)
end

local function driver_switched(driver, device)
  device:set_component_to_endpoint_fn(component_to_endpoint)
  device:try_update_metadata({ profile = "christmas-tresp32", provisioning_state = "PROVISIONED" })
  ensure_subscriptions(device)
  local rgb_comp = device.profile.components.rgbMode
  if rgb_comp ~= nil then
    local current_rgb = device:get_latest_state("rgbMode", capabilities.switch.ID, capabilities.switch.switch.NAME)
    if current_rgb == nil then
      device:emit_component_event(rgb_comp, capabilities.switch.switch.off())
    end
  end
  refresh_state(device)
end

local function info_changed(driver, device, event, args)
  device:set_component_to_endpoint_fn(component_to_endpoint)
  ensure_subscriptions(device)
end

-- [[ MATTER ATTRIBUTE HANDLERS ]] --

local function on_off_attr_handler(driver, device, ib, response)
  if ib.data.value ~= nil then
    local main_comp = device.profile.components.main
    if ib.data.value then
      device:emit_component_event(main_comp, capabilities.switch.switch.on())
    else
      device:emit_component_event(main_comp, capabilities.switch.switch.off())
    end
  end
end

local function current_level_attr_handler(driver, device, ib, response)
  if ib.data.value ~= nil then
    local level = ib.data.value
    if level > 0 then
      level = math.max(1, st_utils.round((level / 254.0) * 100.0))
    end
    device:emit_component_event(device.profile.components.main, capabilities.switchLevel.level(level))
  end
end

local function color_temp_mireds_attr_handler(driver, device, ib, response)
  if ib.data.value ~= nil then
    local rgb_comp = device.profile.components.rgbMode
    if rgb_comp ~= nil then
      if ib.data.value <= MIREDS_RGB_THRESHOLD then
        device:emit_component_event(rgb_comp, capabilities.switch.switch.on())
      else
        device:emit_component_event(rgb_comp, capabilities.switch.switch.off())
      end
    end
  end
end

-- [[ SMARTTHINGS CAPABILITY HANDLERS ]] --

local function handle_switch_on(driver, device, cmd)
  local ep = find_light_endpoint(device)
  if cmd.component == "rgbMode" then
    log.info(string.format("Christmas TrESP32: RGB Toggle -> ON (EP %d, mireds=%d)", ep, MIREDS_RGB_ON))
    device:send(clusters.ColorControl.server.commands.MoveToColorTemperature(
      device, ep, MIREDS_RGB_ON, ZERO_TRANSITION_TIME, OPTIONS_MASK, HANDLE_COMMAND_IF_OFF
    ))
    return
  end

  local now = os.time()
  device:set_field(FIELD_LAST_OFF_CMD_TS, 0)

  local last_level_ts = device:get_field(FIELD_LAST_LEVEL_CMD_TS) or 0
  local pref_on_pct = tonumber(device.preferences and device.preferences.preferredOnLevel) or 0

  -- Apply Preferred Turn-On Dim Level unless a Routine just sent an explicit setLevel
  if pref_on_pct >= 1 and pref_on_pct <= 100 and (now - last_level_ts) > ROUTINE_COALESCE_WINDOW_SEC then
    local matter_level = pct_to_matter_level(pref_on_pct)
    log.info(string.format("Christmas TrESP32: Master Power -> ON at preferred %d%% (EP %d)", pref_on_pct, ep))
    device:send(clusters.LevelControl.server.commands.MoveToLevelWithOnOff(
      device, ep, matter_level, ZERO_TRANSITION_TIME, OPTIONS_MASK, HANDLE_COMMAND_IF_OFF
    ))
  else
    log.info(string.format("Christmas TrESP32: Master Power -> ON (EP %d)", ep))
    device:send(clusters.OnOff.server.commands.On(device, ep))
  end
end

local function handle_switch_off(driver, device, cmd)
  local ep = find_light_endpoint(device)
  if cmd.component == "rgbMode" then
    log.info(string.format("Christmas TrESP32: RGB Toggle -> OFF / Warm White (EP %d, mireds=%d)", ep, MIREDS_WARM_WHITE))
    device:send(clusters.ColorControl.server.commands.MoveToColorTemperature(
      device, ep, MIREDS_WARM_WHITE, ZERO_TRANSITION_TIME, OPTIONS_MASK, HANDLE_COMMAND_IF_OFF
    ))
    return
  end

  local now = os.time()
  device:set_field(FIELD_LAST_OFF_CMD_TS, now)

  log.info(string.format("Christmas TrESP32: Master Power -> OFF (EP %d)", ep))
  device:send(clusters.OnOff.server.commands.Off(device, ep))

  local last_level_ts = device:get_field(FIELD_LAST_LEVEL_CMD_TS) or 0
  local pref_off_pct = tonumber(device.preferences and device.preferences.preferredOffLevel) or 0

  -- Optionally pre-stage a preferred dim level while OFF (without turning the lights back on)
  if pref_off_pct >= 1 and pref_off_pct <= 100 and (now - last_level_ts) > ROUTINE_COALESCE_WINDOW_SEC then
    local matter_level = pct_to_matter_level(pref_off_pct)
    log.info(string.format("Christmas TrESP32: Pre-staging dim level while OFF -> %d%% (EP %d)", pref_off_pct, ep))
    device:send(clusters.LevelControl.server.commands.MoveToLevel(
      device, ep, matter_level, ZERO_TRANSITION_TIME, OPTIONS_MASK, HANDLE_COMMAND_IF_OFF
    ))
  end
end

local function handle_set_level(driver, device, cmd)
  local ep = find_light_endpoint(device)
  local now = os.time()

  if cmd.args.level <= 0 then
    device:set_field(FIELD_LAST_OFF_CMD_TS, now)
    device:send(clusters.OnOff.server.commands.Off(device, ep))
    return
  end

  local level = pct_to_matter_level(cmd.args.level)
  local rate = cmd.args.rate or ZERO_TRANSITION_TIME
  device:set_field(FIELD_LAST_LEVEL_CMD_TS, now)

  local last_off_ts = device:get_field(FIELD_LAST_OFF_CMD_TS) or 0
  if (now - last_off_ts) <= ROUTINE_COALESCE_WINDOW_SEC then
    -- A Routine simultaneously sent "Turn Off" + "Dimmer %": pre-stage CurrentLevel while staying OFF
    log.info(string.format("Christmas TrESP32: Routine Off + Dimmer -> pre-stage %d%% while OFF (EP %d)", cmd.args.level, ep))
    device:send(clusters.LevelControl.server.commands.MoveToLevel(
      device, ep, level, rate, OPTIONS_MASK, HANDLE_COMMAND_IF_OFF
    ))
  else
    device:send(clusters.LevelControl.server.commands.MoveToLevelWithOnOff(
      device, ep, level, rate, OPTIONS_MASK, HANDLE_COMMAND_IF_OFF
    ))
  end
end

local function handle_step_level(driver, device, cmd)
  local step_size = st_utils.round((((cmd.args and cmd.args.stepSize) or 0) / 100.0) * 254.0)
  if step_size == 0 then
    return
  end
  local ep = find_light_endpoint(device)
  local step_mode = step_size > 0 and clusters.LevelControl.types.StepMode.UP or clusters.LevelControl.types.StepMode.DOWN
  device:send(clusters.LevelControl.server.commands.Step(
    device, ep, step_mode, math.abs(step_size), DEFAULT_STEP_TRANSITION_TIME, OPTIONS_MASK, IGNORE_COMMAND_IF_OFF
  ))
end

local function handle_refresh(driver, device, cmd)
  refresh_state(device)
end

local christmas_tree_driver_template = {
  lifecycle_handlers = {
    init = device_init,
    added = device_added,
    doConfigure = do_configure,
    driverSwitched = driver_switched,
    infoChanged = info_changed,
  },
  matter_handlers = {
    attr = {
      [clusters.OnOff.ID] = {
        [clusters.OnOff.attributes.OnOff.ID] = on_off_attr_handler,
      },
      [clusters.LevelControl.ID] = {
        [clusters.LevelControl.attributes.CurrentLevel.ID] = current_level_attr_handler,
      },
      [clusters.ColorControl.ID] = {
        [clusters.ColorControl.attributes.ColorTemperatureMireds.ID] = color_temp_mireds_attr_handler,
      },
    },
  },
  subscribed_attributes = subscribed_attributes,
  capability_handlers = {
    [capabilities.switch.ID] = {
      [capabilities.switch.commands.on.NAME] = handle_switch_on,
      [capabilities.switch.commands.off.NAME] = handle_switch_off,
    },
    [capabilities.switchLevel.ID] = {
      [capabilities.switchLevel.commands.setLevel.NAME] = handle_set_level,
    },
    [capabilities.statelessSwitchLevelStep.ID] = {
      [capabilities.statelessSwitchLevelStep.commands.stepLevel.NAME] = handle_step_level,
    },
    [capabilities.refresh.ID] = {
      [capabilities.refresh.commands.refresh.NAME] = handle_refresh,
    },
  },
  supported_capabilities = {
    capabilities.switch,
    capabilities.switchLevel,
    capabilities.statelessSwitchLevelStep,
    capabilities.refresh,
  },
  shared_device_thread_enabled = true,
}

local driver = MatterDriver("pixelpaw-christmas-tresp32", christmas_tree_driver_template)
log.info_with({ hub_logs = true }, string.format("Starting %s Edge driver", driver.NAME))
driver:run()
