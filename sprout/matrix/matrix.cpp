/**
 * @file matrix.cpp Implementation of matrix-gateway
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

#include "log.h"
#include "constants.h"
#include "matrix.h"
#include "matrixutils.h"
#include <boost/regex.hpp>
#include "httpstack.h"

Matrix::Matrix(const std::string& home_server,
               const std::string& as_token,
               HttpResolver* resolver,
               const std::string local_http,
               LoadMonitor *load_monitor,
               LastValueCache* stats_aggregator) :
  Sproutlet("matrix"),
  _home_server(home_server),
  _tsx_map(),
  _transaction_handler(this),
  _user_handler(this),
  _connection(home_server,
              as_token,
              resolver,
              load_monitor,
              stats_aggregator)
{
  HttpStack* http_stack = HttpStack::get_instance();
  try
  {
    http_stack->register_handler("^/matrix/transactions/$",
                                 &_transaction_handler);
    http_stack->register_handler("^/matrix/users/$",
                                 &_user_handler);
  }
  catch (HttpStack::Exception& e)
  {
    LOG_ERROR("Caught HttpStack::Exception - %s - %d\n", e._func, e._rc);
  }
}

/// Creates a MatrixTsx instance.
SproutletTsx* Matrix::get_tsx(SproutletTsxHelper* helper,
                              const std::string& alias,
                              pjsip_msg* req)
{
  SproutletTsx* tsx;
  if (alias == "matrix")
  {
    MatrixTsx::Config config(this, _home_server, &_connection);
    tsx = new MatrixTsx(helper, config);
  }
  else if (alias == "matrix-outbound")
  {
    MatrixOutboundTsx::Config config(this, _home_server, &_connection);
    tsx = new MatrixOutboundTsx(helper, config);
  }
  return tsx;
}

void Matrix::add_tsx(std::string room_id, MatrixTsx* tsx)
{
  _tsx_map[room_id] = tsx;
}

void Matrix::remove_tsx(std::string room_id)
{
  _tsx_map.erase(room_id);
}

MatrixTsx* Matrix::get_tsx(std::string room_id)
{
  MatrixTsx* tsx = NULL;
  auto tsxs = _tsx_map.find(room_id);
  if (tsxs != _tsx_map.end())
  {
    tsx = (*tsxs).second;
  }
  return tsx;
}

MatrixTsx::MatrixTsx(SproutletTsxHelper* helper, Config& config) :
  SproutletTsx(helper), _config(config)
{
}

void MatrixTsx::add_record_route(pjsip_msg* msg)
{
  pj_pool_t* pool = get_pool(msg);
  MatrixUtils::add_record_route(msg, pool, get_reflexive_uri(pool), _room_id);
}

void MatrixTsx::add_contact(pjsip_msg* msg, pjsip_uri* uri)
{
  pjsip_contact_hdr* contact_hdr = pjsip_contact_hdr_create(get_pool(msg));
  contact_hdr->uri = (pjsip_uri*)uri;
  pjsip_msg_add_hdr(msg, (pjsip_hdr*)contact_hdr);
}

/// Matrix receives an initial request.
void MatrixTsx::on_rx_initial_request(pjsip_msg* req)
{
  // Find the called party in the request URI.
  pjsip_uri* to_uri = req->line.req.uri;
  if (!PJSIP_URI_SCHEME_IS_SIP(to_uri))
  {
    LOG_DEBUG("Request URI isn't a SIP URI");
    pjsip_msg* rsp = create_response(req, PJSIP_SC_TEMPORARILY_UNAVAILABLE);
    send_response(rsp);
    free_msg(req);
    return;
  }
  std::string to_matrix_user = MatrixUtils::matrix_uri_to_matrix_user(to_uri, _config.home_server);

  pjsip_uri* from_uri = MatrixUtils::get_from_uri(req);
  if (from_uri == NULL)
  {
    pjsip_msg* rsp = create_response(req, PJSIP_SC_TEMPORARILY_UNAVAILABLE);
    send_response(rsp);
    free_msg(req);
    return;
  }
  std::string from_matrix_user = MatrixUtils::ims_uri_to_matrix_user(from_uri, _config.home_server);
  _from_matrix_user = from_matrix_user;

  // Get the call ID.
  pjsip_cid_hdr* call_id_hdr = (pjsip_cid_hdr*)pjsip_msg_find_hdr_by_name(req,
                                                                          &STR_CALL_ID,
                                                                          NULL);
  std::string call_id = PJUtils::pj_str_to_string(&call_id_hdr->id);
  std::string matrix_call_id = call_id;
  size_t at_pos = matrix_call_id.find('@');
  if (at_pos != matrix_call_id.npos)
  {
    matrix_call_id.replace(at_pos, 1, "-");
  }
  // TODO Should probably do this in the constructor.
  _call_id = call_id;

  // Get the expiry (if present).
  pjsip_expires_hdr* expires_hdr = (pjsip_expires_hdr*)pjsip_msg_find_hdr(req, PJSIP_H_EXPIRES, NULL);
  int expires = (expires_hdr != NULL) ? expires_hdr->ivalue : 180;
  _expires = expires;

  // Get the SDP body (if an INVITE).
  if (req->line.req.method.id == PJSIP_INVITE_METHOD)
  {
    if ((req->body == NULL ||
        (pj_stricmp2(&req->body->content_type.type, "application") != 0) ||
        (pj_stricmp2(&req->body->content_type.subtype, "sdp") != 0)))
    {
      LOG_DEBUG("No SDP body");
      pjsip_msg* rsp = create_response(req, PJSIP_SC_TEMPORARILY_UNAVAILABLE);
      send_response(rsp);
      free_msg(req);
      return;
    }
    //std::string body = std::string((const char*)req->body->data, req->body->len);
    //std::string sdp;
    //std::vector<std::string> candidates;
    //MatrixUtils::parse_sdp(body, sdp, candidates);
    std::string sdp = std::string((const char*)req->body->data, req->body->len);
    _event_type = MatrixConnection::EVENT_TYPE_CALL_INVITE;
    _event = _config.connection->build_call_invite_event(_call_id, sdp, _expires * 1000);
  }
  else if (!pj_strcmp2(&req->line.req.method.name, "MESSAGE"))
  {
    if ((req->body == NULL ||
        (pj_stricmp2(&req->body->content_type.type, "text") != 0) ||
        (pj_stricmp2(&req->body->content_type.subtype, "plain") != 0)))
    {
      LOG_DEBUG("No text/plan MESSAGE body");
      pjsip_msg* rsp = create_response(req, PJSIP_SC_TEMPORARILY_UNAVAILABLE);
      send_response(rsp);
      free_msg(req);
      return;
    }
    std::string message = std::string((const char*)req->body->data, req->body->len);
    _event_type = MatrixConnection::EVENT_TYPE_MESSAGE;
    _event = _config.connection->build_message_event(message);
  }
  else
  {
    LOG_DEBUG("Unsupported method: %.*s", req->line.req.method.name.slen, req->line.req.method.name.ptr);
    pjsip_msg* rsp = create_response(req, PJSIP_SC_TEMPORARILY_UNAVAILABLE);
    send_response(rsp);
    free_msg(req);
    return;
  }

  LOG_DEBUG("Call from %s (%s) to %s (%s)", PJUtils::uri_to_string(PJSIP_URI_IN_ROUTING_HDR, from_uri).c_str(), from_matrix_user.c_str(), PJUtils::uri_to_string(PJSIP_URI_IN_REQ_URI, to_uri).c_str(), to_matrix_user.c_str());
  LOG_DEBUG("Call ID is %s, expiry is %d", call_id.c_str(), expires);

  HTTPCode rc = _config.connection->register_user(MatrixUtils::matrix_user_to_userpart(from_matrix_user),
                                                  trail());
  // TODO Check and log response

  std::string room_alias = MatrixUtils::get_room_alias(from_matrix_user, to_matrix_user, _config.home_server);
  rc = _config.connection->get_room_for_alias(room_alias,
                                              _room_id,
                                              trail());
  // TODO Check and log response

  if (_room_id.empty())
  {
    std::vector<std::string> invites;
    invites.push_back(to_matrix_user);
    rc = _config.connection->create_room(from_matrix_user,
                                         "Call from " + PJUtils::uri_to_string(PJSIP_URI_IN_ROUTING_HDR, from_uri),
                                         "",
                                         invites,
                                         _room_id,
                                         trail());

    // Work around https://matrix.org/jira/browse/SYN-340 by creating alias manually.
    rc = _config.connection->create_alias(_room_id,
                                          room_alias,
                                          trail());
  }
  else
  {
    // TODO Get members and, if not already a member, invite them
    _config.connection->invite_user(from_matrix_user,
                                    to_matrix_user,
                                    _room_id,
                                    trail());

    rc = _config.connection->send_event(_from_matrix_user,
                                        _room_id,
                                        _event_type,
                                        _event,
                                        trail());
  }

  _config.matrix->add_tsx(_room_id, this);

  //std::string call_candidates_event = _config.connection->build_call_candidates_event(call_id,
  //                                                                                    candidates);
  //rc = _config.connection->send_event(from_matrix_user,
  //                                    _room_id,
  //                                    MatrixConnection::EVENT_TYPE_CALL_CANDIDATES,
  //                                    call_candidates_event,
  //                                    trail());


  if (_event_type == MatrixConnection::EVENT_TYPE_CALL_INVITE)
  {
    pjsip_msg* rsp = create_response(req, PJSIP_SC_RINGING);
    add_record_route(rsp);
    add_contact(rsp, req->line.req.uri);
    send_response(rsp);
    free_msg(req);
    schedule_timer(NULL, _timer_request, expires * 1000);
  }
  else
  {
    pjsip_msg* rsp = create_response(req, PJSIP_SC_OK);
    add_record_route(rsp);
    add_contact(rsp, req->line.req.uri);
    send_response(rsp);
    free_msg(req);
  }
}

/// Matrix receives a response. It will add all the Via headers from the
/// original request back on and send the response on. It can also change
/// the response in various ways depending on the configuration that was
/// specified in the Route header of the original request.
/// - It can mangle the dialog identifiers using its mangalgorithm.
/// - It can mangle the Contact URI using its mangalgorithm.
/// - It can mangle the Record-Route and Route headers URIs.
void MatrixTsx::on_rx_response(pjsip_msg* rsp, int fork_id)
{
  // Should never happen, but be on the safe side and just forward it.
  send_response(rsp);
}

/// Matrix receives an in dialog request. It will strip off all the Via
/// headers and send the request on. It can also change the request in various
/// ways depending on the configuration in its Route header.
/// - It can mangle the dialog identifiers using its mangalgorithm.
/// - It can mangle the Request URI and Contact URI using its mangalgorithm.
/// - It can mangle the To URI using its mangalgorithm.
/// - It can edit the S-CSCF Route header to turn the request into either an
///   originating or terminating request.
/// - It can edit the S-CSCF Route header to turn the request into an out of
///   the blue request.
/// - It can mangle the Record-Route headers URIs.
void MatrixTsx::on_rx_in_dialog_request(pjsip_msg* req)
{
  const pjsip_route_hdr* route = route_hdr();
  if ((route != NULL) &&
      (is_uri_reflexive(route->name_addr.uri)))
  {
    pjsip_sip_uri* uri = (pjsip_sip_uri*)route->name_addr.uri;
    pj_str_t room_str = pj_str("room");
    pjsip_param* param = pjsip_param_find(&uri->other_param, &room_str);
    if (param != NULL)
    {
      _room_id = PJUtils::pj_str_to_string(&param->value);
    }
  }

  pjsip_uri* from_uri = MatrixUtils::get_from_uri(req);
  if (from_uri == NULL)
  {
    pjsip_msg* rsp = create_response(req, PJSIP_SC_TEMPORARILY_UNAVAILABLE);
    send_response(rsp);
    free_msg(req);
    return;
  }
  std::string from_matrix_user = MatrixUtils::ims_uri_to_matrix_user(from_uri, _config.home_server);

  // Get the call ID.
  pjsip_cid_hdr* call_id_hdr = (pjsip_cid_hdr*)pjsip_msg_find_hdr_by_name(req,
                                                                          &STR_CALL_ID,
                                                                          NULL);
  std::string call_id = PJUtils::pj_str_to_string(&call_id_hdr->id);

  if (req->line.req.method.id == PJSIP_BYE_METHOD)
  {
    std::string call_hangup_event = _config.connection->build_call_hangup_event(call_id);
    HTTPCode rc = _config.connection->send_event(from_matrix_user,
                                                 _room_id,
                                                 MatrixConnection::EVENT_TYPE_CALL_HANGUP,
                                                 call_hangup_event,
                                                 trail());

    pjsip_msg* rsp = create_response(req, PJSIP_SC_OK);
    send_response(rsp);
    free_msg(req);
  }
  else
  {
    // TODO: Support other methods.
    pjsip_msg* rsp = create_response(req, PJSIP_SC_NOT_IMPLEMENTED);
    send_response(rsp);
    free_msg(req);
  }
}

void MatrixTsx::rx_matrix_event(const std::string& type, const std::string& user, const std::string& call_id, const std::string& sdp)
{
  LOG_DEBUG("Received matrix event %s for user %s on call ID %s with SDP %s", type.c_str(), user.c_str(), call_id.c_str(), sdp.c_str());
  if ((type == "m.room.member") &&
      (user != _from_matrix_user))
  {
    // TODO: Might not need this branch any more - might just be able to send imediately in all cases.
    // TODO: Adjust expires of INVITE event according to time passed
    HTTPCode rc = _config.connection->send_event(_from_matrix_user,
                                                 _room_id,
                                                 MatrixConnection::EVENT_TYPE_CALL_INVITE,
                                                 _event,
                                                 trail());
  }
  else if (type == "m.call.answer")
  {
    _answer_sdp = sdp;
    // TODO Not sure I'm supposed to call this method when not in context, but from code-reading it should work.
    // TODO Should maybe turn this into a formal "prod" API rather than explicitly abusing timers.
    cancel_timer(_timer_request);
    schedule_timer(NULL, _timer_now, 0);
  }
  else if (type == "m.call.hangup")
  {
    cancel_timer(_timer_request);
    schedule_timer(NULL, _timer_now, 0);
  }
  else
  {
    LOG_DEBUG("Ignoring event of type %s on call %s", type.c_str(), _call_id.c_str());
  }
}

void MatrixTsx::on_rx_cancel(int status_code, pjsip_msg* cancel_req)
{
  std::string call_hangup_event = _config.connection->build_call_hangup_event(_call_id);
  HTTPCode rc = _config.connection->send_event(_from_matrix_user,
                                               _room_id,
                                               MatrixConnection::EVENT_TYPE_CALL_HANGUP,
                                               call_hangup_event,
                                               trail());

  free_msg(cancel_req);
}

// TODO It would be nice if on_timer_expiry gave you the TimerId that had expired
void MatrixTsx::on_timer_expiry(void* context)
{
  pjsip_msg* req = original_request();
  pjsip_msg* rsp;
  if (!_answer_sdp.empty())
  {
    rsp = create_response(req, PJSIP_SC_OK);
    add_record_route(rsp);
    add_contact(rsp, req->line.req.uri);

    pj_str_t mime_type = pj_str("application");
    pj_str_t mime_subtype = pj_str("sdp");
    pj_str_t content;
    pj_cstr(&content, _answer_sdp.c_str());
    rsp->body = pjsip_msg_body_create(get_pool(req), &mime_type, &mime_subtype, &content);
  }
  else
  {
    rsp = create_response(req, PJSIP_SC_TEMPORARILY_UNAVAILABLE);
  }
  send_response(rsp);
  free_msg(req);
}

void MatrixTsx::add_call_id_to_trail(SAS::TrailId trail)
{
  SAS::Marker cid_marker(trail, MARKER_ID_SIP_CALL_ID, 1u);
  cid_marker.add_var_param(_call_id);
  SAS::report_marker(cid_marker, SAS::Marker::Scope::Trace);
}

MatrixOutboundTsx::MatrixOutboundTsx(SproutletTsxHelper* helper, Config& config) :
  SproutletTsx(helper), _config(config)
{
}

/// Matrix receives an initial request.
void MatrixOutboundTsx::on_rx_initial_request(pjsip_msg* req)
{
  // TODO: get true S-CSCF URI - don't assume it's local
  pj_pool_t* pool = get_pool(req);
  pjsip_sip_uri* uri = get_reflexive_uri(pool);
  pj_strdup2(pool, &uri->user, "scscf");
  PJUtils::add_route_header(req, uri, get_pool(req));
  send_request(req);
}

void MatrixOutboundTsx::on_rx_response(pjsip_msg* rsp, int fork_id)
{
  int st_code = rsp->line.status.code;

  std::string event_type;
  std::string event;

  if (st_code == PJSIP_SC_OK)
  {
    std::string sdp = std::string((const char*)rsp->body->data, rsp->body->len);
    event_type = MatrixConnection::EVENT_TYPE_CALL_INVITE;
    event = _config.connection->build_call_answer_event(_call_id, sdp);
  }
  else
  {
    event_type = MatrixConnection::EVENT_TYPE_CALL_HANGUP;
  }
  HTTPCode rc = _config.connection->send_event(_user,
                                               _room_id,
                                               event_type,
                                               event,
                                               trail());

  // Forward the response, although it will go into the void.
  send_response(rsp);
}

void MatrixOutboundTsx::on_rx_in_dialog_request(pjsip_msg* req)
{
  send_request(req);
}

void MatrixOutboundTsx::add_record_route(pjsip_msg* msg)
{
  pj_pool_t* pool = get_pool(msg);
  MatrixUtils::add_record_route(msg, pool, get_reflexive_uri(pool), _room_id);
}
