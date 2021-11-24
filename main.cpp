//
// main.cpp
// ~~~~~~~~
//
// Copyright (c) 2003-2020 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include "server.hpp"
#include <appdynamics.h>

int main(int argc, char* argv[])
{
  try
  {
    // Check command line arguments.
    if (argc != 4)
    {
      std::cerr << "Usage: http_server <address> <port> <doc_root>\n";
      std::cerr << "  For IPv4, try:\n";
      std::cerr << "    receiver 0.0.0.0 80 .\n";
      std::cerr << "  For IPv6, try:\n";
      std::cerr << "    receiver 0::0 80 .\n";
      return 1;
    }

	appd_config *cfg = appd_config_init();
	appd_config_set_app_name(cfg, getenv("APPDYNAMICS_AGENT_APPLICATION_NAME"));
	appd_config_set_tier_name(cfg, getenv("APPDYNAMICS_AGENT_TIER_NAME"));
	appd_config_set_node_name(cfg, getenv("APPDYNAMICS_AGENT_NODE_NAME"));
	appd_config_set_controller_host(cfg, getenv("APPDYNAMICS_CONTROLLER_HOST_NAME"));
	appd_config_set_controller_port(cfg, atoi(getenv("APPDYNAMICS_CONTROLLER_PORT")));
	appd_config_set_controller_account(cfg, getenv("APPDYNAMICS_AGENT_ACCOUNT_NAME"));
	appd_config_set_controller_access_key(cfg, getenv("APPDYNAMICS_AGENT_ACCOUNT_ACCESS_KEY"));
	if (strcmp(getenv("APPDYNAMICS_CONTROLLER_SSL_ENABLED"), "true") == 0) {
		std::cout << "SSL enabled\n";
		appd_config_set_controller_use_ssl(cfg, 1);
	}
	else {
		std::cout << "SSL disabled\n";
		appd_config_set_controller_use_ssl(cfg, 0);
	}

	appd_config_set_logging_min_level(cfg, APPD_LOG_LEVEL_TRACE);
	appd_config_set_init_timeout_ms(cfg, 60000);

	// No need to check for errors from the SDK. 
	// All SDK functions are error-proof: 
	// we can call appd_bt_begin, etc. even if appd_sdk_init
	// failed (which might happen if the provided config is wrong).
	if (appd_sdk_init(cfg) == -1) {
		std::cout << "Failed to initiliaze AppD SDK\n";
		return -1;
	}

	// declare, add backend
	const char backendOne[] = "RabbitMQ";
	appd_backend_declare(APPD_BACKEND_RABBITMQ, backendOne);
	int rc = appd_backend_set_identifying_property(backendOne, "HOST", "localhost");
	rc = appd_backend_set_identifying_property(backendOne, "PORT", "8081");
	rc = appd_backend_set_identifying_property(backendOne, "EXCHANGE", "MyExchange");
	if (rc) {
		std::cerr << "Backend identifying properties could not be set\n";
		return -1;
	}
	rc = appd_backend_prevent_agent_resolution(backendOne);
	if (rc) {
		std::cerr << "Error: appd_backend_prevent_agent_resolution: " << rc << ".";
		return -1;
	}

	// add the backend
	rc = appd_backend_add(backendOne);
	if (rc)
	{
		std::cerr << "Error: appd_backend_add: " << rc << ".";
		appd_sdk_term();
		return -1;
	}

	if (getenv("APPDYNAMICS_UPSTREAM_TIER") == NULL) 
	{
		const char backendTwo[] = "http://ext-api.stoloto.ru";
		appd_backend_declare(APPD_BACKEND_HTTP, backendTwo);
		int rc = appd_backend_set_identifying_property(backendTwo, "HOST", "ext-api.stoloto.ru");
		rc = appd_backend_set_identifying_property(backendTwo, "PORT", "80");
		if (rc) {
			std::cerr << "Backend identifying properties could not be set\n";
			return -1;
		}
		rc = appd_backend_prevent_agent_resolution(backendTwo);
		if (rc) {
			std::cerr << "Error: appd_backend_prevent_agent_resolution: " << rc << ".";
			return -1;
		}

		// add the backend
		rc = appd_backend_add(backendTwo);
		if (rc)
		{
			std::cerr << "Error: appd_backend_add: " << rc << ".";
			appd_sdk_term();
			return -1;
		}
	}

    // Initialise the server.
    http::server::server s(argv[1], argv[2], argv[3]);

    // Run the server until stopped.
    s.run();

	appd_sdk_term();
  }
  catch (std::exception& e)
  {
    std::cerr << "exception: " << e.what() << "\n";
  }

  return 0;
}
