//
// Copyright (C) 2015 Red Hat, Inc.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// Authors: Daniel Kopecek <dkopecek@redhat.com>
//
#pragma once

#include <Typedefs.hpp>
#include <USB.hpp>
#include <Rule.hpp>
#include <RuleSet.hpp>
#include <DeviceManager.hpp>

#include <string>
#include <map>
#include <vector>
#include <cstdint>

namespace usbguard
{
  class DLL_PUBLIC Interface
  {
  public:
    /* Methods */
    virtual uint32_t appendRule(const std::string& rule_spec,
				uint32_t parent_id) = 0;

    virtual void removeRule(uint32_t id) = 0;

    virtual const RuleSet listRules(const std::string& query) = 0;

    virtual uint32_t applyDevicePolicy(uint32_t id,
                                   Rule::Target target,
                                   bool permanent) = 0;

    virtual const std::vector<Rule> listDevices(const std::string& query) = 0;

    /* Signals */
    virtual void DevicePresenceChanged(uint32_t id,
                                       DeviceManager::EventType event,
                                       Rule::Target target,
                                       const std::string& device_rule) = 0;

    virtual void DevicePolicyChanged(uint32_t id,
                                     Rule::Target target_old,
                                     Rule::Target target_new,
                                     const std::string& device_rule,
                                     uint32_t rule_id) = 0;

    virtual void ExceptionMessage(const std::string& context,
                                  const std::string& object,
                                  const std::string& reason) = 0;
  };
} /* namespace usbguard */
