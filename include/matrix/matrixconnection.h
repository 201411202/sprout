/**
 * @file matrixconnection.h MatrixConnection class definition.
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2014  Metaswitch Networks Ltd
 *
 * Parts of this module were derived from GPL licensed PJSIP sample code
 * with the following copyrights.
 *   Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 *   Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#ifndef MATRIXCONNECTION_H__
#define MATRIXCONNECTION_H__

extern "C" {
#include <pjsip.h>
#include <pjlib-util.h>
#include <pjlib.h>
#include <stdint.h>
}

#include "pjutils.h"
#include "stack.h"
#include "httpconnection.h"

/// Definition of MatrixTsx class.
class MatrixConnection
{
public:
  static const std::string EVENT_TYPE_CALL_INVITE;
  static const std::string EVENT_TYPE_CALL_CANDIDATES;
  static const std::string EVENT_TYPE_CALL_ANSWER;
  static const std::string EVENT_TYPE_CALL_HANGUP;
  static const std::string EVENT_TYPE_MESSAGE;

  MatrixConnection(const std::string& home_server,
                   const std::string& as_token,
                   HttpResolver* resolver,
                   LoadMonitor *load_monitor,
                   LastValueCache* stats_aggregator);
  virtual ~MatrixConnection() {};

  HTTPCode register_user(const std::string& userpart,
                         const SAS::TrailId trail = 0);
  HTTPCode get_room_for_alias(const std::string& alias,
                              std::string& id,
                              const SAS::TrailId trail);
  HTTPCode create_room(const std::string& user,
                       const std::string& name,
                       const std::string& alias,
                       const std::vector<std::string>& invites,
                       std::string& id,
                       const SAS::TrailId trail = 0,
                       bool is_public = false,
                       const std::string& topic = "");
  HTTPCode create_alias(const std::string& id,
                        const std::string& alias,
                        const SAS::TrailId trail);
  std::string build_call_invite_event(const std::string& call_id,
                                      const std::string& sdp,
                                      const int lifetime);
  std::string build_call_candidates_event(const std::string& call_id,
                                          const std::vector<std::string>& candidates);
  std::string build_call_answer_event(const std::string& call_id,
                                      const std::string& sdp);
  std::string build_call_hangup_event(const std::string& call_id);
  std::string build_message_event(const std::string& message);
  HTTPCode send_event(const std::string& user,
                      const std::string& room,
                      const std::string& event_type,
                      const std::string& event_body,
                      const SAS::TrailId trail = 0);
  HTTPCode invite_user(const std::string& inviting_user,
                       const std::string& invited_user,
                       const std::string& room,
                       const SAS::TrailId trail = 0);
  HTTPCode join_room(const std::string& user,
                     const std::string& room,
                     const SAS::TrailId trail = 0);

private:
  std::string build_register_user_req(const std::string& userpart);
  HTTPCode parse_get_room_rsp(const std::string& response,
                              std::string& id);
  std::string build_create_alias_req(const std::string& id);
  std::string build_create_room_req(const std::string& name,
                                    const std::string& alias,
                                    const std::vector<std::string>& invites,
                                    const bool is_public = false,
                                    const std::string& topic = "");
  HTTPCode parse_create_room_rsp(const std::string& response,
                                 std::string& id);
  std::string build_invite_user_req(const std::string& invited_user);
  HttpConnection _http;
  std::string _as_token;
};

#endif
