/* Copyright (C) 2012, 2013  Olga Yakovleva <yakovleva.o.v@gmail.com> */

/* This program is free software: you can redistribute it and/or modify */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation, either version 3 of the License, or */
/* (at your option) any later version. */

/* This program is distributed in the hope that it will be useful, */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the */
/* GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License */
/* along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <memory>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <iterator>
#include <algorithm>
#include "tclap/CmdLine.h"
#include "core/smart_ptr.hpp"
#include "core/engine.hpp"
#include "core/document.hpp"
#include "core/client.hpp"
#include "audio.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "sandbird.h"

using namespace RHVoice;

namespace
{
  class audio_player: public client
  {
  public:
    explicit audio_player(const std::string& path);
    bool play_speech(const short* samples,std::size_t count);
    void finish();
    bool set_sample_rate(int sample_rate);

    int get_sample_rate() const
    {
      return stream.get_sample_rate();
    }

  private:
    audio::playback_stream stream;
  };

  audio_player::audio_player(const std::string& path)
  {
    if(!path.empty())
      {
        stream.set_backend(audio::backend_file);
        stream.set_device(path);
      }
  }

  bool audio_player::set_sample_rate(int sample_rate)
  {
    try
      {
        if(stream.is_open()&&(stream.get_sample_rate()!=sample_rate))
          stream.close();
        stream.set_sample_rate(sample_rate);
        return true;
      }
    catch(...)
      {
        return false;
      }
  }

  bool audio_player::play_speech(const short* samples,std::size_t count)
  {
    try
      {
        if(!stream.is_open())
          stream.open();
        stream.write(samples,count);
        return true;
      }
    catch(...)
      {
        stream.close();
        return false;
      }
  }

  void audio_player::finish()
  {
    if(stream.is_open())
      stream.drain();
  }
}

#define LOG_HTTP 1
#define MAX_QUERY_LENGTH 4096

audio_player* player;
smart_ptr<engine> eng(new engine());
voice_profile* profile;

std::string voiceQueryStr(MAX_QUERY_LENGTH, ' ');
char voiceQueryStr_c[MAX_QUERY_LENGTH];

static int event_handler(sb_Event *e) {

  if (e->type == SB_EV_REQUEST) {
    
    if (LOG_HTTP) {
        printf("%s - %s %s\n", e->address, e->method, e->path);
    }
    
    bool errorFlag = false;
    
    try {
      
      sb_get_var(e->stream, "say", voiceQueryStr_c, MAX_QUERY_LENGTH);
      
      size_t len = strlen(voiceQueryStr_c);
      
      if (len == 0) {
        sb_send_status(e->stream, 404, "ERROR");
        sb_send_header(e->stream, "Content-Type", "application/json");
        sb_writef(e->stream, "{\"status\": \"error\"}");
        return SB_RES_OK;
      }
      
      voiceQueryStr = voiceQueryStr_c;
      
      //std::cout << "len:" << len << std::endl;
      //std::cout << "say: " << voiceQueryStr.c_str() << " : " << voiceQueryStr.length() << std::endl;
      
      /*
      if(ssml_switch.getValue())
        doc=document::create_from_ssml(eng,text_start,text_end,profile);
      else
        doc=document::create_from_plain_text(eng, text_start, text_end, content_text, profile);
      */
      
      std::auto_ptr<document> doc;
      
      doc = document::create_from_plain_text(eng, voiceQueryStr.begin(), voiceQueryStr.end(), RHVoice::content_text, *profile);
      
      doc->set_owner(*player);
      doc->synthesize();
      
      player->finish();
      
    } catch (const std::exception& e) {
      std::cerr << e.what() << std::endl;
      errorFlag = true;
    }
    
    if (errorFlag) {
      sb_send_status(e->stream, 500, "ERROR");
      sb_send_header(e->stream, "Content-Type", "application/json");
      sb_writef(e->stream, "{\"status\": \"error\"}");
    } else {
      sb_send_status(e->stream, 200, "OK");
      sb_send_header(e->stream, "Content-Type", "application/json");
      sb_writef(e->stream, "{\"status\": \"success\"}");
    }
    
  }
  return SB_RES_OK;
}

int main(int argc,const char* argv[]) {
    
  TCLAP::CmdLine cmd("RHVoice HTTP API server");
  TCLAP::ValueArg<std::string> host_arg("H","host","host ip address",false,"127.0.0.1","ip address",cmd);
  TCLAP::ValueArg<std::string> port_arg("P","port","port number",false,"18800","port",cmd);
  TCLAP::ValueArg<unsigned int> timeout_arg("t","timeout","http polling timeout in milliseconds",false,500,"positive integer",cmd);
  
  //TCLAP::SwitchArg ssml_switch("s","ssml","Process as ssml",cmd,false);
  TCLAP::ValueArg<std::string> voice_arg("p","profile","voice profile",false,"","spec",cmd);
  cmd.parse(argc,argv);
  
  // HTTP Server init
  sb_Options opt;
  sb_Server *server;
  
  memset(&opt, 0, sizeof(opt));
  
  opt.host = host_arg.getValue().c_str();
  opt.port = port_arg.getValue().c_str();
  opt.handler = event_handler;
  
  // RHVoice init
  player = new audio_player("");
  profile = new voice_profile();
  
  if(!voice_arg.getValue().empty()) {
    *profile = eng->create_voice_profile(voice_arg.getValue());
  }
  
  server = sb_new_server(&opt);

  if (!server) {
    fprintf(stderr, "failed to initialize server\n");
    exit(EXIT_FAILURE);
  }

  printf("RHVoice HTTP API Server running at http://%s:%s\n", opt.host, opt.port);
  
  //Start mainloop
  for (;;) {
    sb_poll_server(server, timeout_arg.getValue());
  }
  
  //player->finish();
  sb_close_server(server);
  return EXIT_SUCCESS;
}
