/**
 * @file sipresolver.cpp  Implementation of SIP DNS resolver class.
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

#include "log.h"
#include "sipresolver.h"
#include "sas.h"
#include "sproutsasevent.h"

SIPResolver::SIPResolver(DnsCachedResolver* dns_client,
                         int blacklist_duration) :
  BaseResolver(dns_client)
{
  TRC_DEBUG("Creating SIP resolver");

  // Create the NAPTR cache.
  std::map<std::string, int> naptr_services;
  naptr_services["SIP+D2U"] = IPPROTO_UDP;
  naptr_services["SIP+D2T"] = IPPROTO_TCP;
  create_naptr_cache(naptr_services);

  // Create the SRV cache.
  create_srv_cache();

  // Create the blacklist.
  create_blacklist(blacklist_duration);

  TRC_STATUS("Created SIP resolver");
}

SIPResolver::~SIPResolver()
{
  destroy_blacklist();
  destroy_srv_cache();
  destroy_naptr_cache();
}

void SIPResolver::resolve(const std::string& name,
                          int af,
                          int port,
                          int transport,
                          int retries,
                          std::vector<AddrInfo>& targets,
                          SAS::TrailId trail)
{
  int dummy_ttl = 0;
  targets.clear();

  // First determine the transport following the process in RFC3263 section
  // 4.1.
  AddrInfo ai;

  TRC_DEBUG("SIPResolver::resolve for name %s, port %d, transport %d, family %d",
            name.c_str(), port, transport, af);

  if (trail != 0)
  {
    SAS::Event event(trail, SASEvent::SIPRESOLVE_START, 0);
    event.add_var_param(name);
    std::string port_str = std::to_string(port);
    std::string transport_str = get_transport_str(transport);
    event.add_var_param(port_str);
    event.add_var_param(transport_str);
    SAS::report_event(event);
  }

  if (parse_ip_target(name, ai.address))
  {
    // The name is already an IP address, so no DNS resolution is possible.
    // Use specified transport and port or defaults if not specified.
    TRC_DEBUG("Target is an IP address - default port/transport if required");
    ai.transport = (transport != -1) ? transport : IPPROTO_UDP;
    ai.port = (port != 0) ? port : 5060;
    targets.push_back(ai);

    if (trail != 0)
    {
      SAS::Event event(trail, SASEvent::SIPRESOLVE_IP_ADDRESS, 0);
      event.add_var_param(name);
      std::string port_str = std::to_string(ai.port);
      std::string transport_str = get_transport_str(ai.transport);
      event.add_var_param(transport_str);
      event.add_var_param(port_str);
      SAS::report_event(event);
    }
  }
  else
  {
    std::string srv_name;
    std::string a_name = name;

    if (port != 0)
    {
      // Port is specified, so don't do NAPTR or SRV look-ups.  Default transport
      // if required and move straight to A record look-up.
      TRC_DEBUG("Port is specified");
      transport = (transport != -1) ? transport : IPPROTO_UDP;

      if (trail != 0)
      {
        SAS::Event event(trail, SASEvent::SIPRESOLVE_PORT_A_LOOKUP, 0);
        event.add_var_param(name);
        std::string port_str = std::to_string(port);
        std::string transport_str = get_transport_str(transport);
        event.add_var_param(transport_str);
        event.add_var_param(port_str);
        SAS::report_event(event);
      }
    }
    else if (transport == -1)
    {
      // Transport protocol isn't specified, so do a NAPTR lookup for the target.
      TRC_DEBUG("Do NAPTR look-up for %s", name.c_str());

      if (trail != 0)
      {
        SAS::Event event(trail, SASEvent::SIPRESOLVE_NAPTR_LOOKUP, 0);
        event.add_var_param(name);
        SAS::report_event(event);
      }

      NAPTRReplacement* naptr = _naptr_cache->get(name, dummy_ttl);

      if (naptr != NULL)
      {
        // NAPTR resolved to a supported service
        TRC_DEBUG("NAPTR resolved to transport %d", naptr->transport);
        transport = naptr->transport;
        if (strcasecmp(naptr->flags.c_str(), "S") == 0)
        {
          // Do an SRV lookup with the replacement domain from the NAPTR lookup.
          srv_name = naptr->replacement;

          if (trail != 0)
          {
            SAS::Event event(trail, SASEvent::SIPRESOLVE_NAPTR_SUCCESS_SRV, 0);
            event.add_var_param(name);
            event.add_var_param(srv_name);
            std::string transport_str = get_transport_str(naptr->transport);
            event.add_var_param(transport_str);
            SAS::report_event(event);
          }
        }
        else
        {
          // Move straight to A/AAAA lookup of the domain returned by NAPTR.
          a_name = naptr->replacement;

          if (trail != 0)
          {
            SAS::Event event(trail, SASEvent::SIPRESOLVE_NAPTR_SUCCESS_A, 0);
            event.add_var_param(name);
            event.add_var_param(a_name);
            SAS::report_event(event);
          }
        }
      }
      else
      {
        // NAPTR resolution failed, so do SRV lookups for both UDP and TCP to
        // see which transports are supported.
        TRC_DEBUG("NAPTR lookup failed, so do SRV lookups for UDP and TCP");

        if (trail != 0)
        {
          SAS::Event event(trail, SASEvent::SIPRESOLVE_NAPTR_FAILURE, 0);
          event.add_var_param(name);
          SAS::report_event(event);
        }

        std::vector<std::string> domains;
        domains.push_back("_sip._udp." + name);
        domains.push_back("_sip._tcp." + name);
        std::vector<DnsResult> results;
        _dns_client->dns_query(domains, ns_t_srv, results);
        DnsResult& udp_result = results[0];
        TRC_DEBUG("UDP SRV record %s returned %d records",
                  udp_result.domain().c_str(), udp_result.records().size());
        DnsResult& tcp_result = results[1];
        TRC_DEBUG("TCP SRV record %s returned %d records",
                  tcp_result.domain().c_str(), tcp_result.records().size());

        if (!udp_result.records().empty())
        {
          // UDP SRV lookup returned some records, so use UDP transport.
          TRC_DEBUG("UDP SRV lookup successful, select UDP transport");
          transport = IPPROTO_UDP;
          srv_name = udp_result.domain();
        }
        else if (!tcp_result.records().empty())
        {
          // TCP SRV lookup returned some records, so use TCP transport.
          TRC_DEBUG("TCP SRV lookup successful, select TCP transport");
          transport = IPPROTO_TCP;
          srv_name = tcp_result.domain();
        }
        else
        {
          // Neither UDP nor TCP SRV lookup returned any results, so default to
          // UDP transport and move straight to A/AAAA record lookups.
          TRC_DEBUG("UDP and TCP SRV queries unsuccessful, default to UDP");
          transport = IPPROTO_UDP;
        }
      }

      _naptr_cache->dec_ref(name);
    }
    else if (transport == IPPROTO_UDP)
    {
      // Use specified transport and try an SRV lookup.
      if (trail != 0)
      {
        SAS::Event event(trail, SASEvent::SIPRESOLVE_TRANSPORT_SRV_LOOKUP, 0);
        event.add_var_param(name);
        std::string transport_str = get_transport_str(transport);
        event.add_var_param(transport_str);
        SAS::report_event(event);
      }

      DnsResult result = _dns_client->dns_query("_sip._udp." + name, ns_t_srv);

      if (!result.records().empty())
      {
        srv_name = result.domain();
      }
    }
    else if (transport == IPPROTO_TCP)
    {
      // Use specified transport and try an SRV lookup.
      if (trail != 0)
      {
        SAS::Event event(trail, SASEvent::SIPRESOLVE_TRANSPORT_SRV_LOOKUP, 0);
        event.add_var_param(name);
        std::string transport_str = get_transport_str(transport);
        event.add_var_param(transport_str);
        SAS::report_event(event);
      }

      DnsResult result = _dns_client->dns_query("_sip._tcp." + name, ns_t_srv);

      if (!result.records().empty())
      {
        srv_name = result.domain();
      }
    }

    if (srv_name != "")
    {
      TRC_DEBUG("Do SRV lookup for %s", srv_name.c_str());

      if (trail != 0)
      {
        SAS::Event event(trail, SASEvent::SIPRESOLVE_SRV_LOOKUP, 0);
        event.add_var_param(srv_name);
        std::string transport_str = get_transport_str(transport);
        event.add_var_param(transport_str);
        SAS::report_event(event);
      }

      srv_resolve(srv_name, af, transport, retries, targets, dummy_ttl, trail);
    }
    else
    {
      TRC_DEBUG("Perform A/AAAA record lookup only, name = %s", a_name.c_str());
      port = (port != 0) ? port : 5060;

      if (trail != 0)
      {
        SAS::Event event(trail, SASEvent::SIPRESOLVE_A_LOOKUP, 0);
        event.add_var_param(a_name);
        std::string transport_str = get_transport_str(transport);
        std::string port_str = std::to_string(port);
        event.add_var_param(transport_str);
        event.add_var_param(port_str);
        SAS::report_event(event);
      }

      a_resolve(a_name, af, port, transport, retries, targets, dummy_ttl, trail);
    }
  }
}

std::string SIPResolver::get_transport_str(int transport)
{
  if (transport == IPPROTO_UDP)
  {
    return "UDP";
  }
  else if (transport == IPPROTO_TCP)
  {
    return "TCP";
  }
  else
  {
    return "UNKNOWN";
  }
}
