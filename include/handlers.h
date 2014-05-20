/**
 * @file handlers.cpp
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
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

#ifndef HANDLERS_H__
#define HANDLERS_H__

#include "httpstack.h"
#include "chronosconnection.h"
#include "hssconnection.h"
#include "regstore.h"
#include "sipresolver.h"
#include "avstore.h"

/// Common factory for all handlers that deal with chronos timer pops. This is
/// a subclass of ConfiguredHandlerFactory that requests HTTP flows to be
/// logged at detail level.
template<class H, class C>
class ChronosHandlerFactory : public HttpStack::ConfiguredHandlerFactory<H, C>
{
public:
  ChronosHandlerFactory(C* cfg) :
    HttpStack::ConfiguredHandlerFactory<H, C>(cfg)
  {}

  virtual ~ChronosHandlerFactory() {}

  SASEvent::HttpLogLevel sas_log_level(HttpStack::Request& req)
  {
    // Log all chronos flows at detail level.
    return SASEvent::HttpLogLevel::DETAIL;
  }
};

class RegistrationTimeoutHandler : public HttpStack::Handler
{
public:
  struct Config
  {
    Config(RegStore* store, RegStore* remote_store, HSSConnection* hss) :
      _store(store), _remote_store(remote_store), _hss(hss)
      {}
    RegStore* _store;
    RegStore* _remote_store;
    HSSConnection* _hss;
  };

  RegistrationTimeoutHandler(HttpStack::Request& req,
                             const Config* cfg,
                             SAS::TrailId trail) :
    HttpStack::Handler(req, trail), _cfg(cfg)
  {};

  void run();

protected:
  void handle_response();
  HTTPCode parse_response(std::string body);
  RegStore::AoR* set_aor_data(RegStore* current_store,
                              std::string aor_id,
                              RegStore::AoR* previous_aor_data,
                              RegStore* remote_store,
                              bool update_chronos,
                              bool& all_bindings_expired);

protected:
  const Config* _cfg;
  std::string _aor_id;
  std::string _binding_id;
};

class DeregistrationHandler : public HttpStack::Handler
{
public:
  struct Config
  {
    Config(RegStore* store, RegStore* remote_store, HSSConnection* hss, SIPResolver* sipresolver) :
      _store(store), _remote_store(remote_store), _hss(hss), _sipresolver(sipresolver)
      {}
    RegStore* _store;
    RegStore* _remote_store;
    HSSConnection* _hss;
    SIPResolver* _sipresolver;
  };


  DeregistrationHandler(HttpStack::Request& req,
                        const Config* cfg,
                        SAS::TrailId trail) :
    HttpStack::Handler(req, trail), _cfg(cfg)
  {};

  void run();
  HTTPCode handle_request(std::string& body);
  HTTPCode parse_request(std::string body);
  RegStore::AoR* set_aor_data(RegStore* current_store,
                              std::string aor_id,
                              std::string private_id,
                              RegStore::AoR* previous_aor_data,
                              RegStore* remote_store,
                              bool is_primary,
                              bool& removed_some_bindings,
                              std::map<std::string, std::string>* emergency_bindings);

protected:
  const Config* _cfg;
  std::map<std::string, std::string> _bindings;
  std::string _notify;
};

class AuthTimeoutHandler : public HttpStack::Handler
{
public:
  struct Config
  {
  Config(AvStore* store, HSSConnection* hss) :
    _avstore(store), _hss(hss) {}
    AvStore* _avstore;
    HSSConnection* _hss;
  };
  AuthTimeoutHandler(HttpStack::Request& req,
                     const Config* cfg,
                     SAS::TrailId trail) :
    HttpStack::Handler(req, trail), _cfg(cfg)
  {};

  void run();
protected:
  HTTPCode handle_response(std::string body);
  const Config* _cfg;
  std::string _impi;
  std::string _impu;
  std::string _nonce;

};

#endif
