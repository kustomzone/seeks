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
 **/

#ifndef SE_HANDLER_H
#define SE_HANDLER_H

#include "proxy_dts.h" // sp_err 

#include <string>
#include <vector>
#include <stdint.h>

using sp::sp_err;

namespace seeks_plugins
{
   class se_parser;
   class search_snippet;
   class query_context;
   
#define NSEs 3  // number of supported search engines.
   
#ifndef ENUM_SE
#define ENUM_SE
   enum SE
     {
	GOOGLE,  // 0
	CUIL,   // 1
	BING     // 2
     };
#endif
   
   class search_engine
     {
      public:
	search_engine();
	virtual ~search_engine();
	
	virtual void query_to_se(const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
				 std::string &url) {};
	
	/*- variables. -*/
	std::string _description;
	bool _anonymous;  // false by default.
	hash_map<const char*,const char*,hash<const char*>,eqstr> *_param_translation;
     };
   
   class se_ggle : public search_engine
     {
      public:
	se_ggle();
	~se_ggle();
	
	virtual void query_to_se(const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
				 std::string &url);
     };
   
   class se_bing : public search_engine
     {
      public:
	se_bing();
	~se_bing();
	
	virtual void query_to_se(const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
				 std::string &url);
     };

   class se_cuil : public search_engine
     {
      public:
	se_cuil();
	~se_cuil();
	
	virtual void query_to_se(const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
				 std::string &url);
     };
   
   class se_handler
     {
      public:
	/*-- query preprocessing --*/
	static void preprocess_parameters(const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters);
	
	/*-- querying the search engines. --*/
	static char** query_to_ses(const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
				                                      int &nresults);
	
	static void query_to_se(const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
				const SE &se, std::string &url);
	
	/*-- parsing --*/
	static se_parser* create_se_parser(const SE &se);
	
	static sp_err parse_ses_output(char **outputs, const int &nresults,
				       std::vector<search_snippet*> &snippets,
				       const int &count_offset,
				       query_context *qr);

	// arguments to a threaded parser.
	struct ps_thread_arg
	  {
	     ps_thread_arg()
	       :_se((SE)0),_output(NULL),_snippets(NULL),_qr(NULL)
	     {};
	
	     ~ps_thread_arg()
	       {
		  // we do not delete the output, this is handled by the client.
		  // we do delete snippets outside the destructor (depends on whethe we're using threads).
	       }
	     	     
	     SE _se; // search engine (ggle, bing, ...).
	     char *_output; // page content, to be parsed into snippets.
	     std::vector<search_snippet*> *_snippets; // websearch result snippets. 
	     int _offset; // offset to snippets rank (when asking page x, with x > 1).
	     query_context *_qr; // pointer to the current query context.
	  };
		
	static void parse_output(const ps_thread_arg &args);
	
	/*-- variables. --*/
      public:
	static std::string _se_strings[NSEs];
	static short _results_lookahead_factor; // we fetch x * the requested number of results.
	                                      // this is because reranking may move to the top/bottom
					      // more results from certain search engines, and less from
					      // others.
	
	/* search engine objects. */
	static se_ggle _ggle;
	static se_cuil _cuil;
	static se_bing _bing;
     };
      
} /* end of namespace. */

#endif