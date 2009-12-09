/**
 * The Seeks proxy and plugin framework are part of the SEEKS project.
 * Copyright (C) 2009 Emmanuel Benazera, juban@free.fr
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */
 
#include "plugin_manager.h"

#include "seeks_proxy.h"
#include "proxy_configuration.h"
#include "errlog.h"

#include "plugin.h"
#include "interceptor_plugin.h"
#include "action_plugin.h"
#include "filter_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>  // linux, TODO: windows.

#include <list>
#include <iostream>

#include <assert.h>

using sp::proxy_configuration;

namespace sp
{
   // variables.
   std::vector<plugin*> plugin_manager::_plugins = std::vector<plugin*>();
   std::vector<interceptor_plugin*> plugin_manager::_ref_interceptor_plugins 
     = std::vector<interceptor_plugin*>();
   std::vector<action_plugin*> plugin_manager::_ref_action_plugins
     = std::vector<action_plugin*>();
   std::vector<filter_plugin*> plugin_manager::_ref_filter_plugins
     = std::vector<filter_plugin*>();

   hash_map<const char*,cgi_dispatcher*,hash<const char*>,eqstr> plugin_manager::_cgi_dispatchers
     = hash_map<const char*,cgi_dispatcher*,hash<const char*>,eqstr>();
   
   std::string plugin_manager::_plugin_repository = "";
   
   std::map<std::string,maker_ptr*,std::less<std::string> > plugin_manager::_factory
     = std::map<std::string,maker_ptr*,std::less<std::string> >();
     
   std::list<void*> plugin_manager::_dl_list = std::list<void*>();
   
   std::map<std::string,configuration_spec*,std::less<std::string> > plugin_manager::_configurations
     = std::map<std::string,configuration_spec*,std::less<std::string> >();
   
   std::string plugin_manager::_config_html_template = "templates/pm_config.html";
   
   int plugin_manager::load_all_plugins()
     {
	// basedir.
	assert(seeks_proxy::_basedir);
	
	plugin_manager::_plugin_repository = std::string(seeks_proxy::_basedir)
#ifdef unix
	+ "/plugins/";
#endif
#if defined(_WIN32)
	+ "\plugins\\";
#endif		
	unsigned int BUF_SIZE = 1024;
	
	// TODO: win32...
	
	std::string command_str = "find " + plugin_manager::_plugin_repository + " -name *.so";
	FILE *dl = popen(command_str.c_str(), "r"); // reading directory.
	if (!dl)
	  {
	     perror("popen");
	     exit(-1);
	  }
	
	void *dlib;
	char name[1024];
	char in_buf[BUF_SIZE]; // input buffer for lib names.
	while(fgets(in_buf, BUF_SIZE, dl))
	  {
	     char *ws = strpbrk(in_buf, " \t\n"); // remove spaces.
	     if (ws) *ws = '\0';
	     
	     sprintf(name, "%s", in_buf); // append './' to lib name.
	     dlib = dlopen(name, RTLD_NOW);  // NOW imposes resolving all symbols
	                                     // required for auto-registration.
	     if (dlib == NULL)
	       {
		  errlog::log_error(LOG_LEVEL_FATAL, "%s", dlerror());
		  exit(-1);
	       }
	     
	     plugin_manager::_dl_list.insert(plugin_manager::_dl_list.end(),dlib); // add lib handle to the list.
	  }
	
	pclose(dl);
	
	//debug
	std::map<std::string,maker_ptr*,std::less<std::string> >::const_iterator mit 
	  = plugin_manager::_factory.begin();
	while(mit!=plugin_manager::_factory.end())
	  {
	     errlog::log_error(LOG_LEVEL_INFO,"loaded plugin \t%s", (*mit).first.c_str());
	     mit++;
	  }
	//debug
	
	plugin_manager::instanciate_plugins();
	
	return 1;
     }
   
   int plugin_manager::close_all_plugins()
     {
	// destroy all plugins that have been created.
	std::vector<plugin*>::iterator vit = plugin_manager::_plugins.begin();
	while(vit!=plugin_manager::_plugins.end())
	  {
	     delete *vit;
	     ++vit;
	  }
	plugin_manager::_plugins.clear();
	
	// close all the opened dynamic libs.
	std::list<void*>::iterator lit = plugin_manager::_dl_list.begin();
	while(lit!=plugin_manager::_dl_list.end())
	  {
	     dlclose((*lit));
	     ++lit;
	  }
	
	return 1;
     }

   int plugin_manager::instanciate_plugins()
     {
	std::map<std::string,maker_ptr*,std::less<std::string> >::const_iterator mit
	  = plugin_manager::_factory.begin();
	while(mit!=plugin_manager::_factory.end())
	  {
	     plugin *p = (*mit).second(); // call to a maker function.
	     
	     // register the plugin object and its functions, if activated.
	     if (seeks_proxy::_config->is_plugin_activated(p->get_name_cstr()))
		 {
		    plugin_manager::register_plugin(p);
		    
		    // register the plugin elements.
		    if (p->_interceptor_plugin)
		      plugin_manager::_ref_interceptor_plugins.push_back(p->_interceptor_plugin);
		    if (p->_action_plugin)
		      plugin_manager::_ref_action_plugins.push_back(p->_action_plugin);
		    if (p->_filter_plugin)
		      plugin_manager::_ref_filter_plugins.push_back(p->_filter_plugin);
		 }
		 
	     ++mit;
	  }
	return 0;
     }

   void plugin_manager::register_plugin(plugin *p)
     {
	plugin_manager::_plugins.push_back(p);
	
	errlog::log_error(LOG_LEVEL_INFO,"Registering plugin %s, and %d CGI dispatchers",
			  p->get_name_cstr(), p->_cgi_dispatchers.size());
	
	std::vector<cgi_dispatcher*>::const_iterator vit = p->_cgi_dispatchers.begin();
	while(vit != p->_cgi_dispatchers.end())
	  {
	     cgi_dispatcher *cgid = (*vit);
	     
	     hash_map<const char*,cgi_dispatcher*,hash<const char*>,eqstr>::iterator hit;
	     if ((hit = plugin_manager::_cgi_dispatchers.find(cgid->_name)) != plugin_manager::_cgi_dispatchers.end())
	       {
		  errlog::log_error(LOG_LEVEL_CGI, "CGI function %s of plugin %s, has already been registered by another plugin.",
				    cgid->_name, p->get_name_cstr());
	       }
	     else
	       {	
		  errlog::log_error(LOG_LEVEL_INFO, "registering CGI dispatcher %s", cgid->_name);
		  
		  plugin_manager::_cgi_dispatchers.insert(std::pair<const char*,cgi_dispatcher*>(cgid->_name,
												 cgid));
	       }
	     		  
	     ++vit;
	  }
     }
   
   cgi_dispatcher* plugin_manager::find_plugin_cgi_dispatcher(const char *path)
     {
	hash_map<const char*,cgi_dispatcher*,hash<const char*>,eqstr>::const_iterator hit;
	if((hit = plugin_manager::_cgi_dispatchers.find(path)) != plugin_manager::_cgi_dispatchers.end())
	  return (*hit).second;
	else 
	  {
	     errlog::log_error(LOG_LEVEL_ERROR, "Can't find any plugin dispatcher in %s", path);
	     return NULL;
	  }
     }
   
   void plugin_manager::get_url_plugins(client_state *csp, http_request *http)
     {
	std::vector<interceptor_plugin*>::const_iterator lit1 
	  = plugin_manager::_ref_interceptor_plugins.begin();
	while(lit1!=plugin_manager::_ref_interceptor_plugins.end())
	  {
	     interceptor_plugin *ip = (*lit1);
#ifdef PLUGIN_DEBUG
	     ip->reload();
#endif
	     if (ip->match_url(http))
	       csp->add_interceptor_plugin(ip);
	     ++lit1;
	  }
	
	std::vector<action_plugin*>::const_iterator lit2
	  = plugin_manager::_ref_action_plugins.begin();
	while(lit2!=plugin_manager::_ref_action_plugins.end())
	  {
	     action_plugin *ip = (*lit2);
#ifdef PLUGIN_DEBUG
	     ip->reload();
#endif
	     if (ip->match_url(http))
	       csp->add_action_plugin(ip);
	     ++lit2;
	  }
	
	std::vector<filter_plugin*>::const_iterator lit3
	  = plugin_manager::_ref_filter_plugins.begin();
	while(lit3!=plugin_manager::_ref_filter_plugins.end())
	  {
	     filter_plugin *ip = (*lit3);
#ifdef PLUGIN_DEBUG
	     ip->reload();
#endif
	     if (ip->match_url(http))
	       csp->add_filter_plugin(ip);
	     ++lit3;
	  }

	// debug
	/* std::cout << "[Debug]:plugin_manager::get_url_plugin: interceptor plugins: " 
	  << csp->_interceptor_plugins.size() << std::endl; */
	//debug
     }

} /* end of namespace. */