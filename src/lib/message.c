/* Copyright (C) 2011  Olga Yakovleva <yakovleva.o.v@gmail.com> */

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

#include <stdlib.h>
#include <string.h>
#include <unistr.h>
#include <expat.h>
#include "lib.h"
#include "vector.h"
#include "ustring.h"

#define ssml_error(s) {s->error_flag=1;XML_StopParser(s->parser,XML_FALSE);return;}

#define rate_min 0.2
#define rate_max 5.0
#define ext_rate_default 20.0
#define pitch_min 0.2
#define pitch_max 2.0
#define ext_pitch_default 50.0
#define volume_min 0.0
#define volume_max 2.0
#define ext_volume_default 50.0

static float ext_rate=ext_rate_default;
static float ext_pitch=ext_pitch_default;
static float ext_volume=ext_volume_default;

static float prosody_check_range(float val,float min,float max)
{
  if(val<min) return min;
  else if(val>max) return max;
  else return val;
}

typedef struct {
  float value;
  int is_absolute;
} prosody_param;

typedef struct {
  prosody_param rate,pitch,volume;
} prosody_params;

static prosody_params default_prosody_params={{1.0,0},{1.0,0},{1.0,0}};

typedef enum {
  token_word=1 << 0,
  token_sentence_start=1 << 1,
  token_sentence_end=1 << 2,
  token_mark=1 << 3,
  token_break=1 << 4,
  token_eol=1 << 5,
  token_eop=1 << 6
} token_flags;

typedef struct {
  unsigned int flags;
  int src_pos,src_len;
  union {
    struct {
      ustring32_t text;
      prosody_params prosody;
    } word;
    uint8_t *mark;
    int duration;
  } contents;
} token;

static void token_free(token *t)
{
  if(t==NULL) return;
  if(t->flags&token_word)
    ustring32_free(t->contents.word.text);
  else if(t->flags&token_mark)
    free(t->contents.mark);
  return;
}

token *token_find_by_flags(token *first,token *last,unsigned int flags)
{
  token *next;
  if((first==NULL)||(last==NULL)) return NULL;
  for(next=first;next<=last;next++)
    {
      if(next->flags&flags) return next;
    }
  return NULL;
}

vector_t(token,toklist)

typedef struct {
  toklist l;
  ucs4_t c;
  unsigned int cs;
} tstream;

static void tstream_init(tstream *ts,toklist tl)
{
  ts->l=tl;
  ts->c='\0';
  ts->cs=cs_sp;
}

static int tstream_putc (tstream *ts,ucs4_t c,size_t src_pos,size_t src_len)
{
  token tok;
  tok.flags=token_word;
  tok.src_pos=src_pos;
  tok.src_len=src_len;
  tok.contents.word.text=NULL;
  tok.contents.word.prosody=default_prosody_params;
  token *prev_tok=toklist_back(ts->l);
  unsigned int cs=classify_character(c);
  if(prev_tok&&(cs&(cs_nl|cs_pr)))
    {
      prev_tok->flags|=token_eol;
      if((cs&cs_pr)||((ts->cs&cs_nl)&&!((ts->c=='\r')&&(c=='\n'))))
        prev_tok->flags|=token_eop;
    }
  if(!(cs&cs_ws))
    {
      if(ts->cs&cs_ws)
        {
          tok.contents.word.text=ustring32_alloc(1);
          if(tok.contents.word.text==NULL) return 0;
          if(!toklist_push(ts->l,&tok))
            {
              ustring32_free(tok.contents.word.text);
              return 0;
            }
        }
      prev_tok=toklist_back(ts->l);
      if(!ustring32_push(prev_tok->contents.word.text,c)) return 0;
      if(src_len>0) prev_tok->src_len=src_pos-prev_tok->src_pos+src_len;
    }
  ts->c=c;
  ts->cs=cs;
  return 1;
}

typedef enum {
  ssml_audio=0,
  ssml_break,
  ssml_desc,
  ssml_emphasis,
  ssml_lexicon,
  ssml_mark,
  ssml_meta,
  ssml_metadata,
  ssml_p,
  ssml_phoneme,
  ssml_prosody,
  ssml_s,
  ssml_say_as,
  ssml_speak,
  ssml_style,
  ssml_sub,
  ssml_voice,
  ssml_unknown,
  ssml_max
} ssml_tag_id;

const char *ssml_tag_names[ssml_max-1]={
  "audio",
  "break",
  "desc",
  "emphasis",
  "lexicon",
  "mark",
  "meta",
  "metadata",
  "p",
  "phoneme",
  "prosody",
  "s",
  "say-as",
  "speak",
  "tts:style",
  "sub",
  "voice"};

static const int ssml_element_table[ssml_max][ssml_max+1]={
  {1,1,1,1,0,1,0,0,1,1,1,1,1,0,1,1,1,0,1},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,0,1,0,1,0,0,0,1,1,0,1,0,1,1,1,0,1},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,1,0,1,0,1,0,0,0,1,1,1,1,0,1,1,1,0,1},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,0,1,0,1,0,0,1,1,1,1,1,0,1,1,1,0,1},
  {1,1,0,1,0,1,0,0,0,1,1,0,1,0,1,1,1,0,1},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,1,0,1,0,1,0,0,1,1,1,1,1,0,1,1,1,0,1},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,0,1,0,1,0,0,1,1,1,1,1,0,1,1,1,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static int ssml_tag_name_cmp(const void *key,const void *element)
{
  const char *s1=(const char*)key;
  const char *s2=*((const char**)element);
  return strcmp(s1,s2);
}

static ssml_tag_id ssml_get_tag_id(const char *name)
{
  const char **found=(const char**)bsearch(name,ssml_tag_names,ssml_max-1,sizeof(const char*),ssml_tag_name_cmp);
  return (found==NULL)?ssml_unknown:(found-&ssml_tag_names[0]);
}

typedef struct {
  uint8_t *name;
  uint8_t *value;
} ssml_attribute;

static void ssml_attribute_free(ssml_attribute *a)
{
  if(a==NULL) return;
  if(a->name!=NULL) free(a->name);
  if(a->value!=NULL) free(a->value);
}

vector_t(ssml_attribute,ssml_attr_list)

static ssml_attr_list ssml_copy_attributes(const char **atts)
{
  ssml_attr_list alist=NULL;
  ssml_attribute a;
  size_t i,n;
  alist=ssml_attr_list_alloc(0,ssml_attribute_free);
  if(alist==NULL) return NULL;
  for(i=0;atts[i]!=NULL;i+=2) {}
  n=i/2;
  if(n==0) return alist;
  if(!ssml_attr_list_reserve(alist,n))
    {
      ssml_attr_list_free(alist);
      return NULL;
    }
  for(i=0;atts[i]!=NULL;i+=2)
    {
      a.name=u8_strdup((const uint8_t*)atts[i]);
      a.value=u8_strdup((const uint8_t*)atts[i+1]);
      ssml_attr_list_push(alist,&a);
      if((a.name==NULL)||(a.value==NULL))
        {
          ssml_attr_list_free(alist);
          return NULL;
        }
    }
  return alist;
}

typedef struct {
  ssml_tag_id id;
  ssml_attr_list attributes;
} ssml_tag;

static void ssml_tag_free(ssml_tag *t)
{
  if(t==NULL) return;
  if(t->attributes!=NULL) ssml_attr_list_free(t->attributes);
}

static const uint8_t *ssml_get_attribute_value(const ssml_tag *t,const char *name)
{
  size_t i,n;
  ssml_attribute *a;
  if(t==NULL) return NULL;
  if(t->attributes==NULL) return NULL;
  for(a=ssml_attr_list_data(t->attributes),i=0,n=ssml_attr_list_size(t->attributes);i<n;i++)
    {
      if(u8_strcmp(a[i].name,(const uint8_t*)name)==0)
        return a[i].value;
    }
  return NULL;
}

vector_t(ssml_tag,ssml_tag_stack)

vector_t(prosody_params,prosody_stack)

typedef union {
  const uint8_t *u8;
  const uint16_t *u16;
  const uint32_t *u32;
} ustr;

typedef struct
{
  int encoding;
  ustr text;
  int len;
  int is_ssml;
} source_info;

typedef struct {
  source_info src;
  const uint8_t *text;
  size_t len;
  XML_Parser parser;
  RHVoice_message msg;
  ssml_tag_stack tags;
  prosody_stack prosody;
  int in_cdata_section;
  int start_sentence;
  unsigned int skip;
  tstream ts;
  size_t text_start;
  size_t text_start_in_chars;
  int error_flag;
} ssml_state;

struct RHVoice_message_s
{
  toklist tokens;
  size_t pos;
};

static RHVoice_message RHVoice_message_alloc(void)
{
  RHVoice_message msg=(RHVoice_message)malloc(sizeof(struct RHVoice_message_s));
  if(msg==NULL) return NULL;
  msg->pos=0;
  msg->tokens=toklist_alloc(16,token_free);
  if(msg->tokens==NULL)
    {
      free(msg);
      return NULL;
    }
  return msg;
}

static void RHVoice_message_free(RHVoice_message msg)
{
  if(msg==NULL) return;
  toklist_free(msg->tokens);
  free(msg);
}

static int prosody_parse_as_number(const char *value,float *number,int *sign,char **suffix)
{
  float res;
  const char *str=value;
  int s=0;
  char *end;
  if(str[0]=='+')
    {
      s=1;
      str++;
    }
  else if(str[0]=='-')
    {
      s=-1;
      str++;
    }
  if(!(((str[0]>='0')&&(str[0]<='9'))||(str[0]=='.'))) return 0;
  res=strtof(str,&end);
  if(end==str) return 0;
  if(res<0) return 0;
  *number=res;
  *sign=s;
  *suffix=end;
  return 1;
}

static prosody_stack prosody_stack_update(prosody_stack stack,const ssml_tag *tag)
{
  prosody_params p=*prosody_stack_back(stack);
  float fval=1.0;
  const char *strval=NULL;
  int sign=0;
  char *suffix=NULL;
  strval=(const char*)ssml_get_attribute_value(tag,"rate");
  if(strval)
    {
      if(strcmp(strval,"default")==0)
        p.rate=default_prosody_params.rate;
      else if(prosody_parse_as_number(strval,&fval,&sign,&suffix))
        {
          if((sign==0)&&(strlen(suffix)==0))
            {
              p.rate.value*=fval;
            }
          else if(strcmp(suffix,"%")==0)
            {
              if(sign!=0) fval=100+sign*fval;
              if(fval<0) fval=0;
              fval/=100.0;
              p.rate.value*=fval;
            }
        }
    }
  strval=(const char*)ssml_get_attribute_value(tag,"pitch");
  if(strval)
    {
      if(strcmp(strval,"default")==0)
        p.pitch=default_prosody_params.pitch;
      else if(prosody_parse_as_number(strval,&fval,&sign,&suffix))
        {
          if(strcmp(suffix,"%")==0)
            {
              if(sign!=0) fval=100+sign*fval;
              if(fval<0) fval=0;
              fval/=100.0;
              p.pitch.value*=fval;
            }
        }
    }
  strval=(const char*)ssml_get_attribute_value(tag,"volume");
  if(strval)
    {
      if(strcmp(strval,"default")==0)
        p.volume=default_prosody_params.volume;
      else if(prosody_parse_as_number(strval,&fval,&sign,&suffix))
        {
          if((sign==0)&&(strlen(suffix)==0))
            {
              if(fval>100) fval=100;
              p.volume.is_absolute=1;
              p.volume.value=fval/50.0;
            }
          else if(strcmp(suffix,"%")==0)
            {
              if(sign!=0) fval=100+sign*fval;
              if(fval<0) fval=0;
              fval/=100.0;
              p.volume.value*=fval;
            }
        }
    }
  return prosody_stack_push(stack,&p);
}

static void XMLCALL ssml_element_start(void *user_data,const char *name,const char **atts)
{
  ssml_state *state=(ssml_state*)user_data;
  if(state->skip) {state->skip++;return;}
  if(!tstream_putc(&state->ts,' ',0,0)) ssml_error(state);
  ssml_tag tag;
  tag.id=ssml_get_tag_id(name);
  int accept=(ssml_tag_stack_size(state->tags)==0)?(tag.id==ssml_speak):ssml_element_table[ssml_tag_stack_back(state->tags)->id][tag.id];
  if(!accept) ssml_error(state);
  if(tag.id==ssml_metadata) {state->skip=1;return;}
  tag.attributes=ssml_copy_attributes(atts);
  if(tag.attributes==NULL) ssml_error(state);
  if(!ssml_tag_stack_push(state->tags,&tag)) ssml_error(state);
  ssml_tag *top=ssml_tag_stack_back(state->tags);
  switch(top->id)
    {
    case ssml_prosody:
      if(!prosody_stack_update(state->prosody,top)) ssml_error(state);
      break;
    case ssml_s:
      state->start_sentence=1;
      break;
    case ssml_p:
      tstream_putc(&state->ts,8233,0,0);
      break;
    default:
      break;
    }
}

static void XMLCALL ssml_element_end(void *user_data,const char *name)
{
  ssml_state *state=(ssml_state*)user_data;
  if(state->skip) {state->skip--;return;}
  if(!tstream_putc(&state->ts,' ',0,0)) ssml_error(state);
  ssml_tag *top=ssml_tag_stack_back(state->tags);
  token *tok=toklist_back(state->msg->tokens);
  switch(top->id)
    {
    case ssml_prosody:
      prosody_stack_pop(state->prosody);
      break;
    case ssml_s:
      if(state->start_sentence) state->start_sentence=0;
      else tok->flags|=token_sentence_end;
      break;
    case ssml_p:
      tstream_putc(&state->ts,8233,0,0);
      break;
    default:
      break;
    }
  ssml_tag_stack_pop(state->tags);
}

static void XMLCALL ssml_character_data(void *user_data,const char *text,int len)
{
  ssml_state *state=(ssml_state*)user_data;
  if(state->skip||(ssml_tag_stack_size(state->tags)==0)) return;
  ssml_tag *top=ssml_tag_stack_back(state->tags);
  if(!ssml_element_table[top->id][ssml_max]) ssml_error(state);
  size_t src_len=XML_GetCurrentByteCount(state->parser);
  size_t src_start=XML_GetCurrentByteIndex(state->parser);
  size_t src_len_in_chars=(src_len==0)?0:u8_mbsnlen(state->text+src_start,src_len);
  size_t src_start_in_chars=state->text_start_in_chars+((src_start>state->text_start)?u8_mbsnlen(state->text+state->text_start,src_start-state->text_start):0);
  int no_refs=state->in_cdata_section?1:((src_len==0)?0:(u8_chr(state->text+src_start,src_len,'&')==NULL));
  size_t r=len;
  size_t p=src_start_in_chars;
  const uint8_t *str=(const uint8_t*)text;
  int n;
  ucs4_t c;
  token *tok=toklist_back(state->msg->tokens);
  while(r>0)
    {
      n=u8_mbtoucr(&c,str,r);
      if(!tstream_putc(&state->ts,c,p,no_refs?1:src_len_in_chars)) ssml_error(state);
      if(tok!=toklist_back(state->msg->tokens))
        {
          tok=toklist_back(state->msg->tokens);
          if(tok->flags&token_word)
            tok->contents.word.prosody=*prosody_stack_back(state->prosody);
          if(state->start_sentence)
            {
              tok->flags|=token_sentence_start;
              state->start_sentence=0;
            }
        }
      r-=n;
      str+=n;
      if(no_refs) p++;
    }
  state->text_start=src_start;
  state->text_start_in_chars=src_start_in_chars;
}

static void XMLCALL ssml_cdata_section_start(void *user_data)
{
  ssml_state *state=(ssml_state*)user_data;
  if(state->skip) return;
  state->in_cdata_section=1;
}

static void XMLCALL ssml_cdata_section_end(void *user_data)
{
  ssml_state *state=(ssml_state*)user_data;
  if(state->skip) return;
  state->in_cdata_section=0;
}

static ssml_state *ssml_state_init(ssml_state *s,const source_info *i,RHVoice_message m)
{
  s->src=*i;
  s->text=NULL;
  s->len=0;
  s->msg=m;
  s->error_flag=0;
  s->skip=0;
  s->in_cdata_section=0;
  s->start_sentence=0;
  s->text_start=0;
  s->text_start_in_chars=0;
  s->parser=XML_ParserCreate("UTF-8");
  if(s->parser==NULL) goto err1;
  s->tags=ssml_tag_stack_alloc(10,ssml_tag_free);
  if(s->tags==NULL) goto err2;
  s->prosody=prosody_stack_alloc(10,NULL);
  if(s->prosody==NULL) goto err3;
  prosody_stack_push(s->prosody,&default_prosody_params);
  switch(s->src.encoding)
    {
    case 16:
      s->text=u16_to_u8(s->src.text.u16,s->src.len,NULL,&s->len);
      break;
    case 32:
      s->text=u32_to_u8(s->src.text.u32,s->src.len,NULL,&s->len);
      break;
    default:
      s->text=s->src.text.u8;
      s->len=s->src.len;
      break;
    }
  if(s->text==NULL) goto err4;
  tstream_init(&s->ts,s->msg->tokens);
  XML_SetUserData(s->parser,s);
  XML_SetElementHandler(s->parser,ssml_element_start,ssml_element_end);
  XML_SetCharacterDataHandler(s->parser,ssml_character_data);
  XML_SetCdataSectionHandler(s->parser,ssml_cdata_section_start,ssml_cdata_section_end);
  return s;
  err4: prosody_stack_free(s->prosody);
  err3: ssml_tag_stack_free(s->tags);
  err2: XML_ParserFree(s->parser);
  err1: return NULL;  
}

static void ssml_state_free(ssml_state *s)
{
  if(s==NULL) return;
  XML_ParserFree(s->parser);
  ssml_tag_stack_free(s->tags);
  prosody_stack_free(s->prosody);
  if(s->src.encoding!=8) free((uint8_t*)s->text);
}

static RHVoice_message parse_ssml(const source_info *i)
{
  ssml_state s;
  int error_flag=0;
  RHVoice_message msg=RHVoice_message_alloc();
  if(msg==NULL) return NULL;
  if(!ssml_state_init(&s,i,msg))
    {
      RHVoice_message_free(msg);
      return NULL;
    }
  size_t r=s.len;
  const char *str=(const char*)s.text;
  size_t n;
  while(r>0)
    {
      n=(r>4096)?4096:r;
      error_flag=(XML_Parse(s.parser,str,n,n==r)==XML_STATUS_ERROR);
      if(error_flag) break;
      error_flag=s.error_flag;
      if(error_flag) break;
      str+=n;
      r-=n;
    }
  ssml_state_free(&s);
  if(error_flag)
    {
      RHVoice_message_free(msg);
      return NULL;
    }
  return msg;
}

static RHVoice_message parse_text(const source_info *i)
{
  tstream ts;
  RHVoice_message msg=RHVoice_message_alloc();
  if(msg==NULL) return NULL;
  ucs4_t c;
  size_t r=i->len;
  size_t p=0;
  int n;
  ustr s=i->text;
  tstream_init(&ts,msg->tokens);
  while(r>0)
    {
      switch(i->encoding)
        {
        case 8:
          n=u8_mbtoucr(&c,s.u8,r);
          break;
        case 16:
          n=u16_mbtoucr(&c,s.u16,r);
          break;
        default:
          n=u32_mbtoucr(&c,s.u32,r);
          break;
        }
      if(!tstream_putc(&ts,c,p,1))
        {
          RHVoice_message_free(msg);
          return NULL;
        }
      r-=n;
      switch(i->encoding)
        {
        case 8:
          s.u8+=n;
          break;
        case 16:
          s.u16+=n;
          break;
        default:
          s.u32+=n;
          break;
        }
      p++;
    }
  return msg;
}

static size_t prepunctuation_length(const ustring32_t t)
{
  if(ustring32_empty(t)) return 0;
  const uint32_t *start=ustring32_str(t);
  const uint32_t *end=start+ustring32_length(t);
  const uint32_t *s=start;
  while(s<end)
    {
      if(!(classify_character(*s)&cs_pi)) break;
      s++;
    }
  return (s-start);
}

static size_t postpunctuation_length(const ustring32_t t)
{
  if(ustring32_empty(t)) return 0;
  const uint32_t *start=ustring32_str(t);
  const uint32_t *end=start+ustring32_length(t);
  const uint32_t *s=end;
  do
    {
      s--;
      if(!(classify_character(*s)&cs_pf)) {s++;break;}
    }
  while(s>start);
  return (end-s);
}

static int is_sentence_boundary(const ustring32_t t1,const ustring32_t t2,int same_line)
{
  const uint32_t *str1=ustring32_str(t1);
  size_t len1=ustring32_length(t1);
  size_t pre1_len=prepunctuation_length(t1);
  size_t post1_len=postpunctuation_length(t1);
  if((post1_len==0)||(pre1_len+post1_len>=len1)) return 0;
  const uint32_t *post1=str1+len1-post1_len;
  const uint32_t *str2=ustring32_str(t2);
  size_t pre2_len=prepunctuation_length(t2);
  int starts_with_cap=(classify_character(str2[pre2_len])&cs_lu);
  if(u32_strchr(post1,'.'))
    {
      if(post1_len==1)
        {
          if(pre2_len==0)
            {
              if(starts_with_cap)
                {
                  if((len1==2)&&(classify_character(str1[0])&cs_lu)) return 0;
                  else return 1;
                }
              else return 0;
            }
          else return 1;
        }
      else return 1;
    }
    else if(u32_strchr(post1,'?')||u32_strchr(post1,'!')) return 1;
    else if ((post1[post1_len-1]==':')&&(pre2_len!=0))
      {
        unsigned int cs=classify_character(str2[0]);
        if((cs&(cs_pq|cs_pi))||(!same_line&&(cs&cs_pd)))
          return 1;
        else return 0;
      }
    else return 0;
}

static void mark_sentence_boundaries(RHVoice_message msg)
{
  if(toklist_size(msg->tokens)==0) return;
  token *next=toklist_front(msg->tokens);
  next->flags|=token_sentence_start;
  token *last=toklist_back(msg->tokens);
  last->flags|=token_sentence_end;
  if(next==last) return;
  token *prev=next;
  next++;
  int skip=0;
  while(next<=last)
    {
      skip=(next->flags&token_sentence_start);
      if(skip||
         (prev->flags&(token_eop|token_sentence_end))||
         is_sentence_boundary(prev->contents.word.text,next->contents.word.text,prev->flags&token_eol))
        {
          prev->flags|=token_sentence_end;
          next->flags|=token_sentence_start;
        }
      if(skip) next=token_find_by_flags(next,last,token_sentence_end);
      prev=next;
      next++;
    }
}

static RHVoice_message new_message(const source_info *i)
{
  RHVoice_message msg;
  msg=(i->is_ssml)?parse_ssml(i):parse_text(i);
  if(msg==NULL) return NULL;
  mark_sentence_boundaries(msg);
  return msg;
}

RHVoice_message RHVoice_new_message_utf8(const uint8_t *text,int len,int is_ssml)
{
  if((text==NULL)||(len<=0)) return NULL;
  source_info i;
  if(u8_check(text,len)!=NULL) return NULL;
  i.encoding=8;
  i.text.u8=text;
  i.len=len;
  i.is_ssml=is_ssml;
  return new_message(&i);
}

RHVoice_message RHVoice_new_message_utf16(const uint16_t *text,int len,int is_ssml)
{
  if((text==NULL)||(len<=0)) return NULL;
  source_info i;
  if(u16_check(text,len)!=NULL) return NULL;
  i.encoding=16;
  i.text.u16=text;
  i.len=len;
  i.is_ssml=is_ssml;
  return new_message(&i);
}

RHVoice_message RHVoice_new_message_utf32(const uint32_t *text,int len,int is_ssml)
{
  if((text==NULL)||(len<=0)) return NULL;
  source_info i;
  if(u32_check(text,len)!=NULL) return NULL;
  i.encoding=32;
  i.text.u32=text;
  i.len=len;
  i.is_ssml=is_ssml;
  return new_message(&i);
}

void RHVoice_delete_message(RHVoice_message msg)
{
  if(msg==NULL) return;
  toklist_free(msg->tokens);
  free(msg);
}

cst_utterance *next_utt_from_message(RHVoice_message msg)
{
  if(msg==NULL) return NULL;
  if(msg->pos>=toklist_size(msg->tokens)) return NULL;
  ustring8_t str8=ustring8_alloc(64);
  if(str8==NULL) return NULL;
  ustring32_t str32;
  const token *front=toklist_front(msg->tokens);
  const token *back=toklist_back(msg->tokens);
  const token *first=toklist_at(msg->tokens,msg->pos);
  const token *last;
  for(last=first;last!=back;last++)
    {
      if(last->flags&token_sentence_end) break;
    }
  cst_utterance *u=new_utterance();
  cst_relation *tr=utt_relation_create(u,"Token");
  cst_item *i;
  size_t len,pre_len,post_len,name_len;
  const token *next;
  float rate=prosody_check_range((ext_rate/ext_rate_default)*first->contents.word.prosody.rate.value,rate_min,rate_max);
  feat_set_float(u->features,"rate",rate);
  float pitch;
  float def_pitch=ext_pitch/ext_pitch_default;
  float volume;
  float def_volume=ext_volume/ext_volume_default;
  for(next=first;next<=last;next++)
    {
      i=relation_append(tr,NULL);
      str32=next->contents.word.text;
      ustring32_substr8(str8,str32,0,0);
      item_set_string(i,"text",(const char*)ustring8_str(str8));
      len=ustring32_length(str32);
      pre_len=prepunctuation_length(str32);
      post_len=postpunctuation_length(str32);
      name_len=(len<=(pre_len+post_len))?0:(len-pre_len-post_len);
      item_set_string(i,"whitespace"," ");
      if(name_len==0)
        item_set_string(i,"name","");
      else
        {
          ustring32_substr8(str8,str32,pre_len,name_len);
          item_set_string(i,"name",(const char*)ustring8_str(str8));
        }
      if(pre_len==0)
        item_set_string(i,"prepunctuation","");
      else
        {
          ustring32_substr8(str8,str32,0,pre_len);
          item_set_string(i,"prepunctuation",(const char*)ustring8_str(str8));
        }
      if(post_len==0)
        item_set_string(i,"punc","");
      else
        {
          ustring32_substr8(str8,str32,pre_len+name_len,post_len);
          item_set_string(i,"punc",(const char*)ustring8_str(str8));
        }
      pitch=prosody_check_range(next->contents.word.prosody.pitch.value*def_pitch,pitch_min,pitch_max);
      item_set_float(i,"pitch",pitch);
      volume=prosody_check_range(next->contents.word.prosody.volume.value*(next->contents.word.prosody.volume.is_absolute?1.0:def_volume),volume_min,volume_max);
      item_set_float(i,"volume",volume);
    }
  if(last==back)
    feat_set_int(u->features,"last",1);
  msg->pos=last-front+1;
  return u;
}

void RHVoice_set_rate(unsigned int rate)
{
  ext_rate=(rate>100)?100:rate;
}

unsigned int RHVoice_get_rate()
{
  return ext_rate;
}

void RHVoice_set_pitch(unsigned int pitch)
{
  ext_pitch=(pitch>100)?100:pitch;
}

unsigned int RHVoice_get_pitch()
{
  return ext_pitch;
}

void RHVoice_set_volume(unsigned int volume)
{
  ext_volume=(volume>100)?100:volume;
}

unsigned int RHVoice_get_volume()
{
  return ext_volume;
}