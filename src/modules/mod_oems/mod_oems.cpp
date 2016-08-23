/*
* Copyright (c) 2016, California Institute of Technology.
* All rights reserved.  Based on Government Sponsored Research under contracts NAS7-1407 and/or NAS7-03001.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
*   1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
*   2. Redistributions in binary form must reproduce the above copyright notice,
*      this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
*   3. Neither the name of the California Institute of Technology (Caltech), its operating division the Jet Propulsion Laboratory (JPL),
*      the National Aeronautics and Space Administration (NASA), nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE CALIFORNIA INSTITUTE OF TECHNOLOGY BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

/*
 * mod_oems.cpp: Mapserver wrapper module for OnEarth
 * Version 1.0
 */

#include "mod_oems.h"

const char *mapfiledir;
static const char *defaultmap = "epsg_all.map";

static const char *mapfile_dir_set(cmd_parms *cmd, void *cfg, const char *arg) {
	mapfiledir = arg;
	return 0;
}

// Retrieve parameter value from URL
void get_param(char *args, char *Name, char *Value) {
	char *pos1 = ap_strcasestr(args, Name);
	if (pos1) {
		pos1 += strlen(Name);
		if (*pos1 == '=') {
			pos1++;
			while (*pos1 && *pos1 != '&') {
				*Value++ = *pos1++;
			}
			*Value++ = '\0';
			return;
		}
	} else {
		Value[0]='\0';
	}
	return;
}

// Return mapfile with full path
char *get_mapfile(request_rec *r, char *mapfile) {
	get_param(r->args, "crs", mapfile); // Use CRS for WMS 1.3
	if(strlen(mapfile) == 0) {
		get_param(r->args, "srs", mapfile); // Use SRS for WMS 1.1
		if(strlen(mapfile) == 0) {
			get_param(r->args, "srsname", mapfile); // Use SRSNAME for WFS
			if(strlen(mapfile) == 0) {
				mapfile = apr_psprintf(r->pool, "%s/%s", mapfiledir, defaultmap);
			}
		}
	}
	if (ap_strstr(mapfile, "4326") != 0) {
		mapfile = apr_psprintf(r->pool, "%s/%s", mapfiledir, "epsg4326.map");
	} else if (ap_strstr(mapfile, "3031") != 0) {
		mapfile = apr_psprintf(r->pool, "%s/%s", mapfiledir, "epsg3031.map");
	} else if (ap_strstr(mapfile, "3413") != 0) {
		mapfile = apr_psprintf(r->pool, "%s/%s", mapfiledir, "epsg3413.map");
	} else if (ap_strstr(mapfile, "3857") != 0) {
		mapfile = apr_psprintf(r->pool, "%s/%s", mapfiledir, "epsg3857.map");
	} else {
		mapfile = apr_psprintf(r->pool, "%s/%s", mapfiledir, defaultmap);
	}
	return mapfile;
}

// Check for valid arguments and transform request for Mapserver
char *validate_args(request_rec *r, char *mapfile) {
	char *args = r->args;
	char proj[4];
	int max_chars;
	max_chars = strlen(r->args) + 1;

	// Time args
	apr_time_exp_t tm = {0};
	apr_size_t tmlen;
	char *time = (char*)apr_pcalloc(r->pool,max_chars);
	char *doytime = (char*)apr_pcalloc(r->pool,max_chars); // Ordinal date with time
	char *productyear = (char*)apr_pcalloc(r->pool,5);

	// General args
	char *service = (char*)apr_pcalloc(r->pool,max_chars);
	char *version = (char*)apr_pcalloc(r->pool,max_chars);
	char *request = (char*)apr_pcalloc(r->pool,max_chars);
	char *format = (char*)apr_pcalloc(r->pool,max_chars);
	char *bbox = (char*)apr_pcalloc(r->pool,max_chars);

	get_param(args,"time",time);
	get_param(args,"service",service);
	get_param(args,"version",version);
	get_param(args,"request",request);
	get_param(args,"format",format);
	get_param(args,"bbox",bbox);

	// Previous args
	char *prev_format = 0;
	char *prev_time = 0;

	// Set notes from previous request if there is one
	if (r->prev != 0) {
		prev_format = (char *) apr_table_get(r->prev->notes, "oems_format");
		prev_time = (char *) apr_table_get(r->prev->notes, "oems_time");
		if (prev_format != 0) {
			ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "prev_format: %s", prev_format);
			apr_table_setn(r->notes, "oems_format", prev_format);
		} else {
			apr_table_setn(r->notes, "oems_format", format);
		}
		if (prev_time != 0) {
			apr_table_setn(r->notes, "oems_time", prev_time);
		}
	} else {
		apr_table_setn(r->notes, "oems_format", format);
	}

	// handle TIME
	if (ap_strchr(time, '-') != 0) {
		int i; i= 0;
		char *times[3];
		char *t;
		char *last;
		t = apr_strtok(time,"-",&last);
		while (t != NULL && i < 4) {
			times[i++] = t;
			t = apr_strtok(NULL,"-",&last);
		}
		// TODO: Handle HH:MM:SS
		time = apr_psprintf(r->pool, "%s-%s-%s", times[0], times[1], times[2]);
//		ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "TIME: %s", time);
		apr_table_setn(r->notes, "oems_time", time);

		tm.tm_year = apr_atoi64(times[0]) - 1900;
		tm.tm_mon = apr_atoi64(times[1]) - 1;
		tm.tm_mday = apr_atoi64(times[2]);

		apr_time_t epoch = 0;
		apr_time_exp_get(&epoch, &tm);
		apr_time_exp_gmt(&tm, epoch);

		apr_strftime(doytime, &tmlen, 14, "%Y%j", &tm);
//		ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "DOYTIME: %s", doytime);
	}

	// check if WMS or WFS
	if (ap_strcasecmp_match(service, "WMS") == 0)  {

		char *transparent = (char*)apr_pcalloc(r->pool,max_chars);
		char *layers = (char*)apr_pcalloc(r->pool,max_chars);
		char *srs = (char*)apr_pcalloc(r->pool,max_chars);
		char *styles = (char*)apr_pcalloc(r->pool,max_chars);
		char *width = (char*)apr_pcalloc(r->pool,max_chars);
		char *height = (char*)apr_pcalloc(r->pool,max_chars);

		char *layer_years = (char*)apr_pcalloc(r->pool,max_chars);
		char *layer_times = (char*)apr_pcalloc(r->pool,max_chars);

		get_param(args,"transparent",transparent);
		get_param(args,"layers",layers);
		get_param(args,"styles",styles);
		get_param(args,"width",width);
		get_param(args,"height",height);

		if (ap_strstr(version, "1.1") != 0) {
			apr_cpystrn(proj, "SRS", 4);
			get_param(args,"srs",srs);
		} else {
			apr_cpystrn(proj, "CRS", 4);
			get_param(args,"crs",srs);
		}
		if(strlen(srs) == 0 || ap_strstr(srs, ":") == 0) {
			apr_cpystrn(srs, "NONE", 5);
			apr_table_setn(r->notes, "oems_srs", 0);
		} else {
			apr_table_setn(r->notes, "oems_srs", srs);
		}

		// Split out layers
		char *layer_cpy = (char*)apr_pcalloc(r->pool,max_chars);
		char *layer_time_param = (char*)apr_pcalloc(r->pool,strlen(layers)+1);
		char *layer_time_value = (char*)apr_pcalloc(r->pool,13);
		char *last_layer = 0;
	    char *prev_last_layer = 0;
	    char *prev_last_layers = 0;
	    if (r->prev != 0) {
	    	prev_last_layer = (char *) apr_table_get(r->prev->notes, "oems_clayer");
	    	prev_last_layers = (char *) apr_table_get(r->prev->notes, "oems_layers");
			last_layer = (char*)apr_pcalloc(r->pool,max_chars+strlen(prev_last_layer));
	    } else {
	    	last_layer = (char*)apr_pcalloc(r->pool,max_chars);
	    }
//	    ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "prev_last_layer: %s", prev_last_layer);
//	    ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "prev_last_layers: %s", prev_last_layers);
	    apr_cpystrn(layer_cpy, layers, strlen(layers)+1);
	    char *pt;
	    char *last;
	    pt = apr_strtok(layer_cpy, ",", &last);
	    while (pt != NULL) {
	    	if (prev_last_layers != 0) {
	    		if (ap_strstr(prev_last_layers, pt) == 0) {
	    			last_layer = pt;
//	    			ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "new layer: %s", last_layer);
	    		}
	    	} else {
		    	last_layer = pt;
	    	}
	    	layer_time_param = apr_psprintf(r->pool,"%s_TIME", pt);
//	    	ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "layer_time: %s", layer_time_param);
	    	get_param(args,layer_time_param,layer_time_value);
	    	if(strlen(layer_time_value) != 0) {
	    		doytime = layer_time_value;
//	    		ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "layer_time_value: %s", layer_time_value);
	    	}
	    	layer_times = apr_psprintf(r->pool,"%s&%s_TIME=%s", layer_times, pt, doytime);
	    	apr_cpystrn(productyear, doytime, 5);
	    	productyear[4] = 0;
	    	layer_years = apr_psprintf(r->pool,"%s&%s_YEAR=%s", layer_years, pt, productyear);
	    	pt = apr_strtok(NULL, ",", &last);
	    }

	    if (prev_last_layer != 0) {
			if (prev_format != 0){
				last_layer = prev_last_layer;
				apr_table_setn(r->notes, "oems_layers", prev_last_layers);
			}
			apr_table_setn(r->notes, "oems_clayer", last_layer);
			if ((strlen(last_layer) != 0) && (prev_format == 0)) {
				last_layer = apr_psprintf(r->pool,"%s,%s", last_layer, prev_last_layers);
				apr_table_setn(r->notes, "oems_layers", last_layer);
			}
	    } else {
	    	apr_table_setn(r->notes, "oems_clayer", last_layer);
	    	apr_table_setn(r->notes, "oems_layers", last_layer);
	    }

//		ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "checked_layers: %s", last_layer);
//	    ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "layer_times: %s", layer_times);
//	    ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "layer_years: %s", layer_years);

	    if (strlen(last_layer) != 0) {
			// Set filters for time snapping if there is a layer that hasn't been checked
		    ap_filter_rec_t *receive_filter = ap_get_output_filter_handle("OEMSTIME_OUT");
		    ap_filter_t *rf = ap_add_output_filter_handle(receive_filter, NULL, r, r->connection);
	    }

		args = apr_psprintf(r->pool,"SERVICE=%s&REQUEST=%s&VERSION=%s&FORMAT=%s&TRANSPARENT=%s&LAYERS=%s&MAP=%s&%s=%s&STYLES=&WIDTH=%s&HEIGHT=%s&BBOX=%s%s%s","WMS",request,version,format,transparent,layers,mapfile,proj,srs,width,height,bbox,layer_times,layer_years);

	} else if (ap_strcasecmp_match(service, "WFS") == 0) {
		char *typenames = (char*)apr_pcalloc(r->pool,max_chars);
		char *outputformat = (char*)apr_pcalloc(r->pool,max_chars);
		char *srsname = (char*)apr_pcalloc(r->pool,max_chars);

		get_param(args,"typenames",typenames);
		if(strlen(typenames) == 0) {
			get_param(args,"typename",typenames);
		}
		get_param(args,"outputformat",outputformat);
		get_param(args,"srsname",srsname);
		if(strlen(srsname) == 0 || ap_strstr(srsname, ":") == 0) {
			apr_cpystrn(srsname, "NONE", 5);
			apr_table_setn(r->notes, "oems_srs", 0);
		} else {
			apr_table_setn(r->notes, "oems_srs", srsname);
		}

		args = apr_psprintf(r->pool,"SERVICE=%s&REQUEST=%s&VERSION=%s&OUTPUTFORMAT=%s&TYPENAMES=%s&BBOX=%s&SRSNAME=%s&MAP=%s&TIME=%s&PRODUCTYEAR=%s","WFS",request,version,outputformat,typenames,bbox,srsname,mapfile,doytime,productyear);
	} else {
		args = r->args;
	}
	return args;
}

// OnEarth Mapserver handler
static int oems_handler(request_rec *r) {
	// Log directory and args for debugging
//	ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "Mapfile Dir: %s", mapfiledir);
//	ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "oems Request args: %s", r->args);

	char *mapfile = (char*)apr_pcalloc(r->pool,strlen(mapfiledir)+16);
	mapfile = get_mapfile(r, mapfile);
	if (mapfile == 0) {
		return DECLINED; // Don't handle if no mapfile found
	}

	// Call Mapserver with mapfile
	r->args = validate_args(r, mapfile);
	ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "Mapserver args: %s", r->args);

	return DECLINED; // Pass request to Mapserver
}

// Main handler for module
static int handler(request_rec *r) {
	if (r->method_number != M_GET) return DECLINED;

	if (!(r->args)) { // Don't handle if no arguments
		return DECLINED;
	} else {
		if ((ap_strstr(r->args, "=WMS") == 0) && (ap_strstr(r->args, "=WFS") == 0)) { // Don't handle if not WMS or WFS
			return DECLINED;
		}
	}
	return oems_handler(r);
}

// Configuration options that go in the httpd.conf
static const command_rec cmds[] =
{
	AP_INIT_TAKE1(
		"MapfileDir",
		(cmd_func) mapfile_dir_set,
		0, /* argument to include in call */
		ACCESS_CONF, /* where available */
		"The directory containing mapfiles" /* help string */
	),
	{NULL}
};

static void register_hooks(apr_pool_t *p) {
	ap_hook_handler(handler, NULL, NULL, APR_HOOK_FIRST);
}

module AP_MODULE_DECLARE_DATA oems_module = {
    STANDARD20_MODULE_STUFF,
    0, 0, 0, 0, cmds, register_hooks
};
