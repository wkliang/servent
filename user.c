/*
 * This file is an example of how to embed web-server functionality
 * into existing application. Compilation line:
 * cc example.c shttpd.c -DEMBEDDED
 */

#ifdef _WIN32
#include <winsock.h>
#define	snprintf			_snprintf

#ifdef _WIN32_WCE
/* Windows CE-specific definitions */
#pragma comment(lib,"ws2")
#endif /* _WIN32_WCE */

#else
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#endif

#ifndef _WIN32_WCE /* Some ANSI #includes are not available on Windows CE */
#include <time.h>
#include <errno.h>
#include <signal.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <limits.h>

#define MY_DEBUGGING	1
#include "defs.h"

#include <tcutil.h>
#include <tchdb.h>
#include <tcbdb.h>
#include <tcfdb.h>
#include <tctdb.h>
#include <tcadb.h>

/*
 * common.[hc]
 */
#if defined(__cplusplus)
#define __COMMON_CLINKAGEBEGIN extern "C" {
#define __COMMON_CLINKAGEEND }
#else
#define __COMMON_CLINKAGEBEGIN
#define __COMMON_CLINKAGEEND
#endif
__COMMON_CLINKAGEBEGIN

#define TPVERSION      "0.9.22"

#define TUNEBNUM       131071            // bnum tuning parameter
#define TUNEAPOW       6                 // apow tuning parameter
#define TUNEFPOW       10                // fpow tuning parameter
#define IOMAXSIZ       (32<<20)          // maximum size of I/O data
#define IOBUFSIZ       65536             // size of an I/O buffer
#define LINEBUFSIZ     1024              // size of a buffer for each line
#define NUMBUFSIZ      64                // size of a buffer for number
#define TINYBNUM       31                // bucket number of a tiny map

enum {                                   // enumeration for external data formats
  FMTWIKI,                               // Wiki
  FMTTEXT,                               // plain text
  FMTHTML                                // HTML
};

#define HEADLVMAX      6                 // maximum level of header
#define SPACELVMAX     8                 // maximum level of spacer
#define IMAGELVMAX     6                 // maximum level of image

/* Load a Wiki string.
   `cols' specifies a map object containing columns.
   `str' specifies the Wiki string. */
void wikiload(TCMAP *cols, const char *str)
{
  assert(cols && str);
  TCLIST *lines = tcstrsplit(tcstrskipspc(str), "\n");
  int lnum = tclistnum(lines);
  int size = 0;
  int bottom = 0;
  for(int i = 0; i < lnum; i++){
    int lsiz;
    const char *line = tclistval(lines, i, &lsiz);
    size += lsiz;
    if(*line != '#' && *tcstrskipspc(line) != '\0') bottom = i;
  }
  TCXSTR *text = tcxstrnew3(size + 1);
  TCXSTR *comments = tcxstrnew3(size + 1);
  int64_t xdate = INT64_MIN;
  bool body = false;
  char numbuf[NUMBUFSIZ];
  for(int i = 0; i < lnum; i++){
    int lsiz;
    const char *line = tclistval(lines, i, &lsiz);
    if(lsiz > 0 && line[lsiz-1] == '\r'){
      lsiz--;
      ((char *)line)[lsiz] = '\0';
    }
    if(*line == '#'){
      const char *rp = line + 1;
      if(*rp == ':'){
        int64_t id = atoi(rp + 1);
        if(id > 0){
          sprintf(numbuf, "%lld", (long long)id);
          tcmapputkeep2(cols, "id", numbuf);
        }
      } else if(*rp == '!' && !body){
        rp = tcstrsqzspc((char *)rp + 1);
        if(*rp != '\0') tcmapputkeep2(cols, "name", rp);
      } else if(*rp == 'c' && !body){
        int64_t date = tcstrmktime(rp + 1);
        if(date != INT64_MIN){
          sprintf(numbuf, "%lld", (long long)date);
          tcmapputkeep2(cols, "cdate", numbuf);
          if(date > xdate) xdate = date;
        }
      } else if(*rp == 'm' && !body){
        int64_t date = tcstrmktime(rp + 1);
        if(date != INT64_MIN){
          sprintf(numbuf, "%lld", (long long)date);
          tcmapputkeep2(cols, "mdate", numbuf);
          if(date > xdate) xdate = date;
        }
      } else if(*rp == 'o' && !body){
        rp = tcstrsqzspc((char *)rp + 1);
        if(*rp != '\0') tcmapputkeep2(cols, "owner", rp);
      } else if(*rp == 't' && !body){
        rp = tcstrsqzspc((char *)rp + 1);
        if(*rp != '\0') tcmapputkeep2(cols, "tags", rp);
      } else if(*rp == '%' && i > bottom){
        rp++;
        char *co = strchr(rp, '|');
        if(co){
          *(co++) = '\0';
          char *ct = strchr(co, '|');
          if(ct){
            *(ct++) = '\0';
            int64_t date = tcstrmktime(rp);
            tcstrtrim(co);
            tcstrtrim(ct);
            if(date != INT64_MIN && *co != '\0' && *ct != '\0'){
              tcxstrprintf(comments, "%lld|%s|%s\n", (long long)date, co, ct);
              if(date > xdate) xdate = date;
            }
          }
        }
      } else if(*rp == '-' || body){
        tcxstrcat(text, line, lsiz);
        tcxstrcat(text, "\n", 1);
      }
    } else if(lsiz > 0 || body){
      tcxstrcat(text, line, lsiz);
      tcxstrcat(text, "\n", 1);
      body = true;
    }
  }
  const char *tbuf = tcxstrptr(text);
  int tsiz = tcxstrsize(text);
  while(tsiz > 0 && tbuf[tsiz-1] == '\n'){
    tsiz--;
  }
  if(tbuf[tsiz] == '\n') tsiz++;
  tcmapputkeep(cols, "text", 4, tbuf, tsiz);
  if(tcxstrsize(comments) > 0)
    tcmapputkeep(cols, "comments", 8, tcxstrptr(comments), tcxstrsize(comments));
  if(xdate != INT64_MIN) tcmapprintf(cols, "xdate", "%lld", (long long)xdate);
  tcxstrdel(comments);
  tcxstrdel(text);
  tclistdel(lines);
}

/* Dump the attributes and the body text of an article into a Wiki string.
   `rbuf' specifies the result buffer.
   `cols' specifies a map object containing columns. */
void wikidump(TCXSTR *rbuf, TCMAP *cols)
{
  assert(rbuf && cols);
  if(tcmaprnum(cols) > 0){
    char numbuf[NUMBUFSIZ];
    const char *val = tcmapget2(cols, "id");
    if(val){
      tcxstrcat(rbuf, "#: ", 3);
      tcxstrcat2(rbuf, val);
      tcxstrcat(rbuf, "\n", 1);
    }
    val = tcmapget2(cols, "name");
    if(val){
      tcxstrcat(rbuf, "#! ", 3);
      tcxstrcat2(rbuf, val);
      tcxstrcat(rbuf, "\n", 1);
    }
    val = tcmapget2(cols, "cdate");
    if(val){
      tcdatestrwww(tcatoi(val), INT_MAX, numbuf);
      tcxstrcat(rbuf, "#c ", 3);
      tcxstrcat2(rbuf, numbuf);
      tcxstrcat(rbuf, "\n", 1);
    }
    val = tcmapget2(cols, "mdate");
    if(val){
      tcdatestrwww(tcatoi(val), INT_MAX, numbuf);
      tcxstrcat(rbuf, "#m ", 3);
      tcxstrcat2(rbuf, numbuf);
      tcxstrcat(rbuf, "\n", 1);
    }
    val = tcmapget2(cols, "owner");
    if(val){
      tcxstrcat(rbuf, "#o ", 3);
      tcxstrcat2(rbuf, val);
      tcxstrcat(rbuf, "\n", 1);
    }
    val = tcmapget2(cols, "tags");
    if(val){
      tcxstrcat(rbuf, "#t ", 3);
      tcxstrcat2(rbuf, val);
      tcxstrcat(rbuf, "\n", 1);
    }
    tcxstrcat(rbuf, "\n", 1);
  }
  int tsiz;
  const char *tbuf = tcmapget(cols, "text", 4, &tsiz);
  if(tbuf && tsiz > 0) tcxstrcat(rbuf, tbuf, tsiz);
  int csiz;
  const char *cbuf = tcmapget(cols, "comments", 8, &csiz);
  if(cbuf && csiz > 0){
    TCLIST *lines = tcstrsplit(cbuf, "\n");
    int cnum = tclistnum(lines);
    if(cnum > 0){
      tcxstrcat(rbuf, "\n", 1);
      for(int i = 0; i < cnum; i++){
        const char *rp = tclistval2(lines, i);
        char *co = strchr(rp, '|');
        if(co){
          *(co++) = '\0';
          char *ct = strchr(co, '|');
          if(ct){
            *(ct++) = '\0';
            char numbuf[NUMBUFSIZ];
            tcdatestrwww(tcatoi(rp), INT_MAX, numbuf);
            tcxstrcat(rbuf, "#% ", 3);
            tcxstrcat2(rbuf, numbuf);
            tcxstrcat(rbuf, "|", 1);
            tcxstrcat2(rbuf, co);
            tcxstrcat(rbuf, "|", 1);
            tcxstrcat2(rbuf, ct);
            tcxstrcat(rbuf, "\n", 1);
          }
        }
      }
    }
    tclistdel(lines);
  }
}

/* Add an inline Wiki string into plain text.
   `rbuf' specifies the result buffer.
   `line' specifies the inline Wiki string. */
void wikitotextinline(TCXSTR *rbuf, const char *line)
{
  assert(rbuf && line);
  while(*line != '\0'){
    const char *pv;
    if(*line == '['){
      if(tcstrfwm(line, "[[") && (pv = strstr(line + 2, "]]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        const char *uri = field;
        char *sep = strchr(field, '|');
        if(sep){
          *sep = '\0';
          uri = sep + 1;
        }
        wikitotextinline(rbuf, field);
        if(strcmp(field, uri)) tcxstrprintf(rbuf, "(%s)", uri);
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[*") && (pv = strstr(line + 2, "*]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrcat2(rbuf, "__");
        wikitotextinline(rbuf, field);
        tcxstrcat2(rbuf, "__");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[\"") && (pv = strstr(line + 2, "\"]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrcat2(rbuf, " \"");
        wikitotextinline(rbuf, field);
        tcxstrcat2(rbuf, "\" ");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[+") && (pv = strstr(line + 2, "+]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrcat2(rbuf, "++");
        wikitotextinline(rbuf, field);
        tcxstrcat2(rbuf, "++");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[-") && (pv = strstr(line + 2, "-]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrcat2(rbuf, "--");
        wikitotextinline(rbuf, field);
        tcxstrcat2(rbuf, "--");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[#") && (pv = strstr(line + 2, "#]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrcat2(rbuf, "##");
        wikitotextinline(rbuf, field);
        tcxstrcat2(rbuf, "##");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[$") && (pv = strstr(line + 2, "$]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrcat2(rbuf, "$$");
        wikitotextinline(rbuf, field);
        tcxstrcat2(rbuf, "$$");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[=") && (pv = strstr(line + 2, "=]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrprintf(rbuf, "%s", field);
        tcfree(field);
        line = pv + 2;
      } else {
        tcxstrcat(rbuf, line, 1);
        line++;
      }
    } else {
      tcxstrcat(rbuf, line, 1);
      line++;
    }
  }
}

/* Convert a Wiki string into a plain text string.
   `rbuf' specifies the result buffer.
   `str' specifies the Wiki string. */
void wikitotext(TCXSTR *rbuf, const char *str)
{
  assert(rbuf && str);
  TCLIST *lines = tcstrsplit(str, "\n");
  int lnum = tclistnum(lines);
  int ri = 0;
  while(ri < lnum){
    int lsiz;
    const char *line = tclistval(lines, ri, &lsiz);
    if(lsiz > 0 && line[lsiz-1] == '\r'){
      lsiz--;
      ((char *)line)[lsiz] = '\0';
    }
    if(*line == '#'){
      ri++;
    } else if(*line == '*'){
      int lv = 1;
      const char *rp = line + 1;
      while(*rp == '*'){
        lv++;
        rp++;
      }
      rp = tcstrskipspc(rp);
      if(*rp != '\0'){
        if(lv == 1){
          tcxstrcat2(rbuf, "[[[ ");
          wikitotextinline(rbuf, rp);
          tcxstrcat2(rbuf, " ]]]\n\n");
        } else if(lv == 2){
          tcxstrcat2(rbuf, "[[ ");
          wikitotextinline(rbuf, rp);
          tcxstrcat2(rbuf, " ]]\n\n");
        } else if(lv == 3){
          tcxstrcat2(rbuf, "[ ");
          wikitotextinline(rbuf, rp);
          tcxstrcat2(rbuf, " ]\n\n");
        } else {
          wikitotextinline(rbuf, rp);
          tcxstrcat2(rbuf, "\n\n");
        }
      }
      ri++;
    } else if(*line == '-' || *line == '+'){
      int ei = ri + 1;
      while(ei < lnum){
        const char *rp = tclistval2(lines, ei);
        if(*rp != '-' && *rp != '+') break;
        ei++;
      }
      for(int i = ri; i < ei; i++){
        const char *rp = tclistval2(lines, i);
        int sep = *rp;
        int clv = 1;
        rp++;
        while(*rp == sep){
          clv++;
          rp++;
        }
        rp = tcstrskipspc(rp);
        for(int j = 0; j < clv; j++){
          tcxstrcat2(rbuf, "  ");
        }
        tcxstrcat2(rbuf, "* ");
        wikitotextinline(rbuf, rp);
        tcxstrcat2(rbuf, "\n");
      }
      tcxstrcat2(rbuf, "\n");
      ri = ei;
    } else if(*line == ',' || *line == '|'){
      int ei = ri + 1;
      while(ei < lnum){
        const char *rp = tclistval2(lines, ei);
        if(*rp != ',' && *rp != '|') break;
        ei++;
      }
      for(int i = ri; i < ei; i++){
        const char *rp = tclistval2(lines, i);
        int sep = *rp;
        rp++;
        tcxstrcat2(rbuf, "  ");
        for(int j = 0; true; j++){
          if(j > 0) tcxstrcat2(rbuf, " | ");
          const char *pv = strchr(rp, sep);
          char *field = pv ? tcmemdup(rp, pv - rp) : tcstrdup(rp);
          tcstrtrim(field);
          wikitotextinline(rbuf, field);
          tcfree(field);
          if(!pv) break;
          rp = pv + 1;
        }
        tcxstrcat2(rbuf, "\n");
      }
      tcxstrcat2(rbuf, "\n");
      ri = ei;
    } else if(*line == '>'){
      int ei = ri + 1;
      while(ei < lnum){
        const char *rp = tclistval2(lines, ei);
        if(*rp != '>') break;
        ei++;
      }
      for(int i = ri; i < ei; i++){
        const char *rp = tclistval2(lines, i);
        rp = tcstrskipspc(rp + 1);
        if(*rp != '\0'){
          tcxstrcat2(rbuf, ">> ");
          wikitotextinline(rbuf, rp);
          tcxstrcat2(rbuf, "\n\n");
        }
      }
      ri = ei;
    } else if(tcstrfwm(line, "{{{")){
      TCXSTR *sep = tcxstrnew();
      line += 3;
      while(*line != '\0'){
        switch(*line){
          case '{': tcxstrprintf(sep, "%c", '}'); break;
          case '[': tcxstrprintf(sep, "%c", ']'); break;
          case '<': tcxstrprintf(sep, "%c", '>'); break;
          case '(': tcxstrprintf(sep, "%c", ')'); break;
          default: tcxstrcat(sep, line, 1); break;
        }
        line++;
      }
      tcxstrcat(sep, "}}}", 3);
      const char *sepstr = tcxstrptr(sep);
      ri++;
      int ei = ri;
      while(ei < lnum){
        const char *rp = tclistval2(lines, ei);
        if(!strcmp(rp, sepstr)) break;
        ei++;
      }
      for(int i = ri; i < ei; i++){
        const char *rp = tclistval2(lines, i);
        tcxstrprintf(rbuf, "  %s\n", rp);
      }
      tcxstrcat(rbuf, "\n", 1);
      tcxstrdel(sep);
      ri = ei + 1;
    } else if(*line == '@'){
      line++;
      if(*line == '@') line++;
      bool obj = false;
      if(*line == '!'){
        obj = true;
        line++;
      }
      while(*line == '<' || *line == '>' || *line == '+' || *line == '-' || *line == '|'){
        line++;
      }
      line = tcstrskipspc(line);
      if(*line != '\0') tcxstrprintf(rbuf, "  [%s:%s]\n\n", obj ? "OBJECT" : "IMAGE", line);
      ri++;
    } else if(tcstrfwm(line, "===")){
      tcxstrcat2(rbuf, "----\n\n");
      ri++;
    } else {
      line = tcstrskipspc(line);
      if(*line != '\0'){
        tcxstrcat2(rbuf, "  ");
        wikitotextinline(rbuf, line);
        tcxstrcat2(rbuf, "\n\n");
        ri++;
      } else {
        ri++;
      }
    }
  }
  tclistdel(lines);
}

/* Dump the attributes and the body text of an article into a plain text string.
   `rbuf' specifies the result buffer.
   `cols' specifies a map object containing columns. */
void wikidumptext(TCXSTR *rbuf, TCMAP *cols)
{
  assert(rbuf && cols);
  if(tcmaprnum(cols) > 0){
    char numbuf[NUMBUFSIZ];
    const char *val = tcmapget2(cols, "id");
    if(val) tcxstrprintf(rbuf, "ID: %lld\n", (long long)tcatoi(val));
    val = tcmapget2(cols, "name");
    if(val) tcxstrprintf(rbuf, "Name: %s\n", val);
    val = tcmapget2(cols, "cdate");
    if(val){
      tcdatestrwww(tcatoi(val), INT_MAX, numbuf);
      tcxstrprintf(rbuf, "Creation Date: %s\n", numbuf);
    }
    val = tcmapget2(cols, "mdate");
    if(val){
      tcdatestrwww(tcatoi(val), INT_MAX, numbuf);
      tcxstrprintf(rbuf, "Modification Date: %s\n", numbuf);
    }
    val = tcmapget2(cols, "owner");
    if(val) tcxstrprintf(rbuf, "Owner: %s\n", val);
    val = tcmapget2(cols, "tags");
    if(val) tcxstrprintf(rbuf, "Tags: %s\n", val);
    tcxstrcat2(rbuf, "----\n\n");
  }
  const char *text = tcmapget2(cols, "text");
  if(text) wikitotext(rbuf, text);
  const char *com = tcmapget2(cols, "comments");
  if(com){
    TCLIST *lines = tcstrsplit(com, "\n");
    int cnum = tclistnum(lines);
    if(cnum > 0){
      for(int i = 0; i < cnum; i++){
        const char *rp = tclistval2(lines, i);
        char *co = strchr(rp, '|');
        if(co){
          *(co++) = '\0';
          char *ct = strchr(co, '|');
          if(ct){
            *(ct++) = '\0';
            char numbuf[NUMBUFSIZ];
            tcdatestrwww(tcatoi(rp), INT_MAX, numbuf);
            tcxstrprintf(rbuf, "# ");
            tcxstrprintf(rbuf, "[%s]: %s: ", numbuf, co);
            wikitotextinline(rbuf, ct);
            tcxstrcat(rbuf, "\n", 1);
          }
        }
      }
    }
    tclistdel(lines);
  }
}

/* Add an inline Wiki string into HTML.
   `rbuf' specifies the result buffer.
   `line' specifies the inline Wiki string.
   `buri' specifies the base URI.
   `duri' specifie the URI of the data directory. */
void wikitohtmlinline(TCXSTR *rbuf, const char *line, const char *buri, const char *duri)
{
  assert(rbuf && line && buri);
  while(*line != '\0'){
    const char *pv;
    if(*line == '['){
      if(tcstrfwm(line, "[[") && (pv = strstr(line + 2, "]]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        const char *uri = field;
        char *sep = strchr(field, '|');
        if(sep){
          *sep = '\0';
          uri = sep + 1;
        }
        if(tcstrfwm(uri, "http://") || tcstrfwm(uri, "https://") ||
           tcstrfwm(uri, "ftp://") || tcstrfwm(uri, "mailto:")){
          tcxstrprintf(rbuf, "<a href=\"%@\">", uri);
        } else if(tcstrfwm(uri, "id:")){
          int64_t id = tcatoi(tcstrskipspc(strchr(uri, ':') + 1));
          tcxstrprintf(rbuf, "<a href=\"%s?id=%lld\">", buri, (long long)(id > 0 ? id : 0));
        } else if(tcstrfwm(uri, "name:")){
          uri = tcstrskipspc(strchr(uri, ':') + 1);
          tcxstrprintf(rbuf, "<a href=\"%s?name=%?\">", buri, uri);
        } else if(tcstrfwm(uri, "param:")){
          uri = tcstrskipspc(strchr(uri, ':') + 1);
          tcxstrprintf(rbuf, "<a href=\"%s%s%@\">", buri, *uri != 0 ? "?" : "", uri);
        } else if(tcstrfwm(uri, "upfile:")){
          uri = tcstrskipspc(strchr(uri, ':') + 1);
          tcxstrprintf(rbuf, "<a href=\"%s/%@\">", duri ? duri : buri, uri);
        } else if(tcstrfwm(uri, "wpen:")){
          uri = tcstrskipspc(strchr(uri, ':') + 1);
          if(*uri == '\0') uri = field;
          tcxstrprintf(rbuf, "<a href=\"http://en.wikipedia.org/wiki/%?\">", uri);
        } else if(tcstrfwm(uri, "wpja:")){
          uri = tcstrskipspc(strchr(uri, ':') + 1);
          if(*uri == '\0') uri = field;
          tcxstrprintf(rbuf, "<a href=\"http://ja.wikipedia.org/wiki/%?\">", uri);
        } else if(sep){
          uri = tcstrskipspc(uri);
          tcxstrprintf(rbuf, "<a href=\"%@\">", uri);
        } else {
          uri = tcstrskipspc(uri);
          tcxstrprintf(rbuf, "<a href=\"%s?name=%?\">", buri, uri);
        }
        wikitohtmlinline(rbuf, field, buri, duri);
        tcxstrprintf(rbuf, "</a>");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[*") && (pv = strstr(line + 2, "*]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrcat2(rbuf, "<strong>");
        wikitohtmlinline(rbuf, field, buri, duri);
        tcxstrcat2(rbuf, "</strong>");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[\"") && (pv = strstr(line + 2, "\"]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrcat2(rbuf, "<cite>");
        wikitohtmlinline(rbuf, field, buri, duri);
        tcxstrcat2(rbuf, "</cite>");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[+") && (pv = strstr(line + 2, "+]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrcat2(rbuf, "<ins>");
        wikitohtmlinline(rbuf, field, buri, duri);
        tcxstrcat2(rbuf, "</ins>");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[-") && (pv = strstr(line + 2, "-]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrcat2(rbuf, "<del>");
        wikitohtmlinline(rbuf, field, buri, duri);
        tcxstrcat2(rbuf, "</del>");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[#") && (pv = strstr(line + 2, "#]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrcat2(rbuf, "<code>");
        wikitohtmlinline(rbuf, field, buri, duri);
        tcxstrcat2(rbuf, "</code>");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[$") && (pv = strstr(line + 2, "$]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrcat2(rbuf, "<var>");
        wikitohtmlinline(rbuf, field, buri, duri);
        tcxstrcat2(rbuf, "</var>");
        tcfree(field);
        line = pv + 2;
      } else if(tcstrfwm(line, "[=") && (pv = strstr(line + 2, "=]")) != NULL){
        char *field = tcmemdup(line + 2, pv - line - 2);
        tcxstrprintf(rbuf, "%@", field);
        tcfree(field);
        line = pv + 2;
      } else {
        switch(*line){
          case '&': tcxstrcat(rbuf, "&amp;", 5); break;
          case '<': tcxstrcat(rbuf, "&lt;", 4); break;
          case '>': tcxstrcat(rbuf, "&gt;", 4); break;
          case '"': tcxstrcat(rbuf, "&quot;", 6); break;
          default: tcxstrcat(rbuf, line, 1); break;
        }
        line++;
      }
    } else {
      switch(*line){
        case '&': tcxstrcat(rbuf, "&amp;", 5); break;
        case '<': tcxstrcat(rbuf, "&lt;", 4); break;
        case '>': tcxstrcat(rbuf, "&gt;", 4); break;
        case '"': tcxstrcat(rbuf, "&quot;", 6); break;
        default: tcxstrcat(rbuf, line, 1); break;
      }
      line++;
    }
  }
}

/* Get the MIME type of a file.
   `name' specifies the name of the file.
   The return value is the MIME type of the file or `NULL' if the type is not detected. */
const char *mimetype(const char *name)
{
  assert(name);
  const char *list[] = {
    "txt", "text/plain", "text", "text/plain", "asc", "text/plain", "in", "text/plain",
    "c", "text/plain", "h", "text/plain", "cc", "text/plain", "java", "text/plain",
    "sh", "text/plain", "pl", "text/plain", "py", "text/plain", "rb", "text/plain",
    "idl", "text/plain", "csv", "text/plain", "log", "text/plain", "conf", "text/plain",
    "rc", "text/plain", "ini", "text/plain",
    "html", "text/html", "htm", "text/html", "xhtml", "text/html", "xht", "text/html",
    "css", "text/css", "js", "text/javascript", "tsv", "text/tab-separated-values",
    "eml", "message/rfc822", "mime", "message/rfc822", "mht", "message/rfc822",
    "mhtml", "message/rfc822", "sgml", "application/sgml",
    "sgm", "application/sgml", "xml", "application/xml", "xsl", "application/xml",
    "xslt", "application/xslt+xml", "xhtml", "application/xhtml+xml",
    "xht", "application/xhtml+xml", "rdf", "application/rdf+xml",
    "rss", "application/rss+xml", "dtd", "application/xml-dtd",
    "rtf", "application/rtf", "pdf", "application/pdf",
    "ps", "application/postscript", "eps", "application/postscript",
    "doc", "application/msword", "xls", "application/vnd.ms-excel",
    "ppt", "application/vnd.ms-powerpoint", "xdw", "application/vnd.fujixerox.docuworks",
    "swf", "application/x-shockwave-flash", "zip", "application/zip",
    "tar", "application/x-tar", "gz", "application/x-gzip",
    "bz2", "application/octet-stream", "z", "application/octet-stream",
    "lha", "application/octet-stream", "lzh", "application/octet-stream",
    "cab", "application/octet-stream", "rar", "application/octet-stream",
    "sit", "application/octet-stream", "bin", "application/octet-stream",
    "o", "application/octet-stream", "so", "application/octet-stream",
    "exe", "application/octet-stream", "dll", "application/octet-stream",
    "class", "application/octet-stream",
    "tch", "application/x-tokyocabinet-hash", "tcb", "application/x-tokyocabinet-btree",
    "tcf", "application/x-tokyocabinet-fixed", "tct", "application/x-tokyocabinet-table",
    "png", "image/png", "jpg", "image/jpeg", "jpeg", "image/jpeg",
    "gif", "image/gif", "tif", "image/tiff", "tiff", "image/tiff", "bmp", "image/bmp",
    "svg", "image/svg+xml", "xbm", "image/x-xbitmap",
    "au", "audio/basic", "snd", "audio/basic", "mid", "audio/midi", "midi", "audio/midi",
    "mp2", "audio/mpeg", "mp3", "audio/mpeg", "wav", "audio/x-wav", "mpg", "video/mpeg",
    "mpeg", "video/mpeg", "mp4", "video/mp4", "qt", "video/quicktime",
    "mov", "video/quicktime", "avi", "video/x-msvideo",
    NULL
  };
  int len = strlen(name);
  char stack[1024];
  char *buf = NULL;
  if(len < sizeof(stack)){
    buf = stack;
  } else {
    buf = tcmalloc(len + 1);
  }
  memcpy(buf, name, len);
  buf[len] = '\0';
  char *pv = strchr(buf, '#');
  if(pv) *pv = '\0';
  pv = strchr(buf, '?');
  if(pv) *pv = '\0';
  const char *ext = strrchr(buf, '.');
  const char *type = NULL;
  if(ext){
    ext++;
    for(int i = 0; list[i] != NULL; i++){
      if(!tcstricmp(ext, list[i])){
        type = list[i+1];
        break;
      }
    }
  }
  if(buf != stack) tcfree(buf);
  return type;
}

/* Convert a Wiki string into an HTML string.
   `rbuf' specifies the result buffer.
   `str' specifies the Wiki string.
   `id' specifies the ID string of the article.  If it is `NULL', the ID is not expressed.
   `buri' specifies the base URI.
   `bhl' specifies the base header level.
   `duri' specifie the URI of the data directory. */
void wikitohtml(TCXSTR *rbuf, const char *str, const char *id, const char *buri, int bhl, const char *duri)
{
  assert(rbuf && str && buri && bhl >= 0);
  TCLIST *lines = tcstrsplit(str, "\n");
  int lnum = tclistnum(lines);
  int headcnts[HEADLVMAX];
  memset(headcnts, 0, sizeof(headcnts));
  int tblcnt = 0;
  int imgcnt = 0;
  int ri = 0;
  while(ri < lnum){
    int lsiz;
    const char *line = tclistval(lines, ri, &lsiz);
    if(lsiz > 0 && line[lsiz-1] == '\r'){
      lsiz--;
      ((char *)line)[lsiz] = '\0';
    }
    if(*line == '#'){
      ri++;
    } else if(*line == '*'){
      int lv = 1;
      const char *rp = line + 1;
      while(*rp == '*'){
        lv++;
        rp++;
      }
      int abslv = lv;
      lv += bhl;
      if(lv > HEADLVMAX) lv = HEADLVMAX;
      rp = tcstrskipspc(rp);
      if(*rp != '\0'){
        headcnts[lv-1]++;
        tcxstrprintf(rbuf, "<h%d", lv);
        if(id){
          tcxstrprintf(rbuf, " id=\"%@", id);
          for(int i = bhl; i < lv; i++){
            tcxstrprintf(rbuf, "_%d", headcnts[i]);
          }
          tcxstrprintf(rbuf, "\"");
        }
        tcxstrprintf(rbuf, " class=\"ah%d topic\">", abslv);
        wikitohtmlinline(rbuf, rp, buri, duri);
        tcxstrprintf(rbuf, "</h%d>\n", lv);
        for(int i = lv; i < HEADLVMAX; i++){
          headcnts[lv] = 0;
        }
      }
      ri++;
    } else if(*line == '-' || *line == '+'){
      int ei = ri + 1;
      while(ei < lnum){
        const char *rp = tclistval2(lines, ei);
        if(*rp != '-' && *rp != '+') break;
        ei++;
      }
      TCLIST *stack = tclistnew();
      for(int i = ri; i < ei; i++){
        const char *rp = tclistval2(lines, i);
        int sep = *rp;
        int clv = 1;
        rp++;
        while(*rp == sep){
          clv++;
          rp++;
        }
        rp = tcstrskipspc(rp);
        if(clv <= tclistnum(stack)) tcxstrcat2(rbuf, "</li>\n");
        while(clv < tclistnum(stack)){
          char *tag = tclistpop2(stack);
          tcxstrprintf(rbuf, "</%s>\n", tag);
          tcfree(tag);
          tcxstrcat2(rbuf, "</li>\n");
        }
        for(int k = 0; clv > tclistnum(stack); k++){
          if(k > 0) tcxstrcat2(rbuf, "<li>\n");
          const char *tag = (sep == '-') ? "ul" : "ol";
          tclistpush2(stack, tag);
          tcxstrprintf(rbuf, "<%s>\n", tag);
        }
        tcxstrcat2(rbuf, "<li>");
        wikitohtmlinline(rbuf, rp, buri, duri);
      }
      while(tclistnum(stack) > 0){
        tcxstrcat2(rbuf, "</li>\n");
        char *tag = tclistpop2(stack);
        tcxstrprintf(rbuf, "</%s>\n", tag);
        tcfree(tag);
      }
      tclistdel(stack);
      ri = ei;
    } else if(*line == ',' || *line == '|'){
      int ei = ri + 1;
      while(ei < lnum){
        const char *rp = tclistval2(lines, ei);
        if(*rp != ',' && *rp != '|') break;
        ei++;
      }
      tcxstrprintf(rbuf, "<table summary=\"table:%d\">\n", ++tblcnt);
      for(int i = ri; i < ei; i++){
        const char *rp = tclistval2(lines, i);
        int sep = *rp;
        rp++;
        tcxstrcat2(rbuf, "<tr>\n");
        while(true){
          tcxstrcat2(rbuf, "<td>");
          const char *pv = strchr(rp, sep);
          char *field = pv ? tcmemdup(rp, pv - rp) : tcstrdup(rp);
          tcstrtrim(field);
          wikitohtmlinline(rbuf, field, buri, duri);
          tcfree(field);
          tcxstrcat2(rbuf, "</td>\n");
          if(!pv) break;
          rp = pv + 1;
        }
        tcxstrcat2(rbuf, "</tr>\n");
      }
      tcxstrcat2(rbuf, "</table>\n");
      ri = ei;
    } else if(*line == '>'){
      int ei = ri + 1;
      while(ei < lnum){
        const char *rp = tclistval2(lines, ei);
        if(*rp != '>') break;
        ei++;
      }
      tcxstrcat2(rbuf, "<blockquote>\n");
      for(int i = ri; i < ei; i++){
        const char *rp = tclistval2(lines, i);
        rp = tcstrskipspc(rp + 1);
        if(*rp != '\0'){
          tcxstrcat2(rbuf, "<p>");
          wikitohtmlinline(rbuf, rp, buri, duri);
          tcxstrcat2(rbuf, "</p>\n");
        }
      }
      tcxstrcat2(rbuf, "</blockquote>\n");
      ri = ei;
    } else if(tcstrfwm(line, "{{{")){
      TCXSTR *sep = tcxstrnew();
      line += 3;
      while(*line != '\0'){
        switch(*line){
          case '{': tcxstrprintf(sep, "%c", '}'); break;
          case '[': tcxstrprintf(sep, "%c", ']'); break;
          case '<': tcxstrprintf(sep, "%c", '>'); break;
          case '(': tcxstrprintf(sep, "%c", ')'); break;
          default: tcxstrcat(sep, line, 1); break;
        }
        line++;
      }
      tcxstrcat(sep, "}}}", 3);
      const char *sepstr = tcxstrptr(sep);
      ri++;
      int ei = ri;
      while(ei < lnum){
        const char *rp = tclistval2(lines, ei);
        if(!strcmp(rp, sepstr)) break;
        ei++;
      }
      tcxstrcat2(rbuf, "<pre>");
      for(int i = ri; i < ei; i++){
        const char *rp = tclistval2(lines, i);
        tcxstrprintf(rbuf, "%@\n", rp);
      }
      tcxstrcat2(rbuf, "</pre>\n");
      tcxstrdel(sep);
      ri = ei + 1;
    } else if(*line == '@'){
      line++;
      imgcnt++;
      bool anc = false;
      if(*line == '@'){
        anc = true;
        line++;
      }
      bool obj = false;
      if(*line == '!'){
        obj = true;
        line++;
      }
      const char *align = "normal";
      int lv = 3;
      if(*line == '<'){
        align = "left";
        line++;
        while(*line == '<' || *line == '>'){
          lv += (*line == '<') ? 1 : -1;
          line++;
        }
      } else if(*line == '>'){
        align = "right";
        line++;
        while(*line == '<' || *line == '>'){
          lv += (*line == '>') ? 1 : -1;
          line++;
        }
      } else if(*line == '+'){
        align = "center";
        line++;
        while(*line == '+' || *line == '-'){
          lv += (*line == '+') ? 1 : -1;
          line++;
        }
      } else if(*line == '|'){
        align = "table";
        line++;
        while(*line == '|'){
          lv++;
          line++;
        }
      }
      if(lv < 1) lv = 1;
      if(lv > IMAGELVMAX) lv = IMAGELVMAX;
      line = tcstrskipspc(line);
      if(*line != '\0'){
        char *uri = tcstrdup(line);
        int width = 0;
        bool wratio = false;
        int height = 0;
        bool hratio = false;
        const char *alt = NULL;
        TCLIST *params = NULL;
        char *sep = strchr(uri, '|');
        if(sep){
          *(sep++) = '\0';
          width = tcatoi(sep);
          while(*sep >= '0' && *sep <= '9'){
            sep++;
          }
          if(*sep == '%') wratio = true;
          sep = strchr(sep, '|');
          if(sep){
            sep++;
            height = tcatoi(sep);
            while(*sep >= '0' && *sep <= '9'){
              sep++;
            }
            if(*sep == '%') hratio = true;
            sep = strchr(sep, '|');
            if(sep){
              sep++;
              alt = sep;
              sep = strchr(sep, '|');
              if(sep){
                *(sep++) = '\0';
                params = tcstrsplit(sep, "|");
              }
            }
          }
        }
        const char *name;
        char *url;
        if(tcstrfwm(uri, "upfile:")){
          const char *rp = strstr(uri, ":") + 1;
          url = tcsprintf("%s/%@", duri ? duri : buri, rp);
          name = rp;
        } else {
          url = tcstrdup(uri);
          name = strrchr(uri, '/');
          name = name ? name + 1 : uri;
        }
        const char *scale = width > 0 ? "sized" : "ratio";
        tcxstrprintf(rbuf, "<div class=\"image image_%s image_%s%d image_%s\">",
                     align, align, lv, scale);
        if(anc) tcxstrprintf(rbuf, "<a href=\"%@\">", url);
        if(obj){
          tcxstrprintf(rbuf, "<object data=\"%@\"", url);
          const char *type = mimetype(url);
          if(type) tcxstrprintf(rbuf, " type=\"%@\"", type);
          if(width > 0) tcxstrprintf(rbuf, " width=\"%d%@\"", width, wratio ? "%" : "");
          if(height > 0) tcxstrprintf(rbuf, " height=\"%d%@\"", height, hratio ? "%" : "");
          tcxstrprintf(rbuf, ">");
          if(params){
            int pnum = tclistnum(params) - 1;
            for(int i = 0; i < pnum; i += 2){
              tcxstrprintf(rbuf, "<param name=\"%@\" value=\"%@\" />",
                           tclistval2(params, i), tclistval2(params, i + 1));
            }
          }
          if(alt){
            tcxstrprintf(rbuf, "%@", alt);
          } else {
            tcxstrprintf(rbuf, "object:%d:%@", imgcnt, name);
          }
          tcxstrprintf(rbuf, "</object>");
        } else {
          tcxstrprintf(rbuf, "<img src=\"%@\"", url);
          if(width > 0) tcxstrprintf(rbuf, " width=\"%d%s\"", width, wratio ? "%" : "");
          if(height > 0) tcxstrprintf(rbuf, " height=\"%d%s\"", height, hratio ? "%" : "");
          if(alt){
            tcxstrprintf(rbuf, " alt=\"%@\"", alt);
          } else {
            tcxstrprintf(rbuf, " alt=\"image:%d:%@\"", imgcnt, name);
          }
          tcxstrprintf(rbuf, " />");
        }
        if(anc) tcxstrprintf(rbuf, "</a>");
        tcxstrprintf(rbuf, "</div>\n");
        tcfree(url);
        if(params) tclistdel(params);
        tcfree(uri);
      }
      ri++;
    } else if(tcstrfwm(line, "===")){
      while(*line == '='){
        line++;
      }
      int lv = 0;
      while(*line == '#'){
        line++;
        lv++;
      }
      if(lv > SPACELVMAX) lv = SPACELVMAX;
      tcxstrprintf(rbuf, "<div class=\"rule rule_s%d\">"
                   "<span>----</span></div>\n", lv);
      ri++;
    } else {
      line = tcstrskipspc(line);
      if(*line != '\0'){
        tcxstrcat2(rbuf, "<p>");
        wikitohtmlinline(rbuf, line, buri, duri);
        tcxstrcat2(rbuf, "</p>\n");
        ri++;
      } else {
        ri++;
      }
    }
  }
  tclistdel(lines);
}

/* Dump the attributes and the body text of an article into an HTML string.
   `rbuf' specifies the result buffer.
   `cols' specifies a map object containing columns.
   `buri' specifies the base URI.
   `bhl' specifies the base header level.
   `duri' specifie the URI of the data directory. */
void wikidumphtml(TCXSTR *rbuf, TCMAP *cols, const char *buri, int bhl, const char *duri)
{
  assert(rbuf && cols && buri && bhl >= 0);
  char idbuf[NUMBUFSIZ];
  const char *val = tcmapget2(cols, "id");
  if(val){
    sprintf(idbuf, "article%lld", (long long)tcatoi(val));
    tcxstrprintf(rbuf, "<div class=\"article\" id=\"%@\">\n", idbuf);
  } else {
    *idbuf = '\0';
    tcxstrprintf(rbuf, "<div class=\"article\">\n");
  }
  if(tcmaprnum(cols) > 0){
    tcxstrprintf(rbuf, "<div class=\"attributes\">\n");
    char numbuf[NUMBUFSIZ];
    val = tcmapget2(cols, "name");
    if(val) tcxstrprintf(rbuf, "<h%d class=\"attr ah0\">%@</h%d>\n", bhl + 1, val, bhl + 1);
    const char *val = tcmapget2(cols, "id");
    if(val) tcxstrprintf(rbuf, "<div class=\"attr\">ID:"
                         " <span class=\"id\">%lld</span></div>\n", (long long)tcatoi(val));
    val = tcmapget2(cols, "cdate");
    if(val){
      tcdatestrwww(tcatoi(val), INT_MAX, numbuf);
      tcxstrprintf(rbuf, "<div class=\"attr\">Creation Date:"
                   " <span class=\"cdate\">%@</span></div>\n", numbuf);
    }
    val = tcmapget2(cols, "mdate");
    if(val){
      tcdatestrwww(tcatoi(val), INT_MAX, numbuf);
      tcxstrprintf(rbuf, "<div class=\"attr\">Modification Date:"
                   " <span class=\"mdate\">%@</span></div>\n", numbuf);
    }
    val = tcmapget2(cols, "owner");
    if(val) tcxstrprintf(rbuf, "<div class=\"attr\">Owner:"
                         " <span class=\"owner\">%@</span></div>\n", val);
    val = tcmapget2(cols, "tags");
    if(val) tcxstrprintf(rbuf, "<div class=\"attr\">Tags:"
                         " <span class=\"tags\">%@</span></div>\n", val);
    tcxstrcat2(rbuf, "</div>\n");
  }
  const char *text = tcmapget2(cols, "text");
  if(text && *text != '\0'){
    tcxstrprintf(rbuf, "<div class=\"text\">\n");
    wikitohtml(rbuf, text, *idbuf != '\0' ? idbuf : NULL, buri, bhl + 1, duri);
    tcxstrcat2(rbuf, "</div>\n");
  }
  const char *com = tcmapget2(cols, "comments");
  if(com && *com != '\0'){
    TCLIST *lines = tcstrsplit(com, "\n");
    int cnum = tclistnum(lines);
    if(cnum > 0){
      tcxstrprintf(rbuf, "<div class=\"comments\">\n");
      int cnt = 0;
      for(int i = 0; i < cnum; i++){
        const char *rp = tclistval2(lines, i);
        char *co = strchr(rp, '|');
        if(co){
          *(co++) = '\0';
          char *ct = strchr(co, '|');
          if(ct){
            *(ct++) = '\0';
            char numbuf[NUMBUFSIZ];
            tcdatestrwww(tcatoi(rp), INT_MAX, numbuf);
            cnt++;
            tcxstrprintf(rbuf, "<div class=\"comment\"");
            if(*idbuf != '\0') tcxstrprintf(rbuf, " id=\"%@_c%d\"", idbuf, cnt);
            tcxstrprintf(rbuf, ">\n");
            tcxstrprintf(rbuf, "<span class=\"date\">%@</span> :\n", numbuf);
            tcxstrprintf(rbuf, "<span class=\"owner\">%@</span> :\n", co);
            tcxstrprintf(rbuf, "<span class=\"text\">");
            wikitohtmlinline(rbuf, ct, buri, duri);
            tcxstrprintf(rbuf, "</span>\n");
            tcxstrprintf(rbuf, "</div>\n");
          }
        }
      }
      tcxstrcat2(rbuf, "</div>\n");
    }
    tclistdel(lines);
  }
  tcxstrcat2(rbuf, "</div>\n");
}

/* Simplify a date string.
   `str' specifies the date string.
   The return value is the date string itself. */
char *datestrsimple(char *str)
{
  assert(str);
  char *pv = str;
  while(*pv > '\0' && *pv <= ' '){
    pv++;
  }
  int len = strlen(pv);
  if(len <= 16 || pv[4] != '-' || pv[7] != '-' || pv[10] != 'T' || pv[13] != ':') return pv;
  pv[4] = '/';
  pv[7] = '/';
  pv[10] = ' ';
  pv[16] = '\0';
  return str;
}

/* Get the human readable name of the MIME type.
   `type' specifies the MIME type.
   The return value is the name of the MIME type. */
const char *mimetypename(const char *type)
{
  const char *rp = strchr(type, '/');
  if(!rp) return "unknown";
  rp++;
  if(tcstrfwm(type, "text/")){
    if(!strcmp(rp, "plain")) return "plain text";
    if(!strcmp(rp, "html")) return "HTML text";
    if(!strcmp(rp, "css")) return "CSS text";
    if(!strcmp(rp, "javascript")) return "JavaScript";
    if(!strcmp(rp, "tab-separated-values")) return "TSV text";
    return "text";
  }
  if(tcstrfwm(type, "message/")){
    if(!strcmp(rp, "rfc822")) return "MIME";
    return "message";
  }
  if(tcstrfwm(type, "application/")){
    if(!strcmp(rp, "sgml")) return "SGML";
    if(!strcmp(rp, "xml")) return "XML";
    if(!strcmp(rp, "xslt+xml")) return "XSLT";
    if(!strcmp(rp, "xhtml+xml")) return "XHTML";
    if(!strcmp(rp, "rdf+xml")) return "RDF";
    if(!strcmp(rp, "rss+xml")) return "RSS";
    if(!strcmp(rp, "xml-dtd")) return "DTD";
    if(!strcmp(rp, "rtf")) return "RTF";
    if(!strcmp(rp, "pdf")) return "PDF";
    if(!strcmp(rp, "postscript")) return "PostScript";
    if(!strcmp(rp, "msword")) return "MS-Word";
    if(!strcmp(rp, "vnd.ms-excel")) return "MS-Excel";
    if(!strcmp(rp, "vnd.ms-powerpoint")) return "MS-PowerPoint";
    if(!strcmp(rp, "vnd.fujixerox.docuworks")) return "FX-DocuWorks";
    if(!strcmp(rp, "x-shockwave-flash")) return "Flash";
    return "binary";
  }
  if(tcstrfwm(type, "image/")){
    if(!strcmp(rp, "png")) return "PNG image";
    if(!strcmp(rp, "jpeg")) return "JPEG image";
    if(!strcmp(rp, "gif")) return "GIF image";
    if(!strcmp(rp, "tiff")) return "TIFF image";
    if(!strcmp(rp, "bmp")) return "BMP image";
    if(!strcmp(rp, "svg+xml")) return "SVG image";
    if(!strcmp(rp, "x-xbitmap")) return "XBM image";
    return "image";
  }
  if(tcstrfwm(type, "audio/")){
    if(!strcmp(rp, "basic")) return "basic audio";
    if(!strcmp(rp, "midi")) return "MIDI audio";
    if(!strcmp(rp, "mpeg")) return "MPEG audio";
    if(!strcmp(rp, "x-wav")) return "WAV audio";
    return "audio";
  }
  if(tcstrfwm(type, "video/")){
    if(!strcmp(rp, "mpeg")) return "MPEG video";
    if(!strcmp(rp, "quicktime")) return "QuickTime";
    if(!strcmp(rp, "x-msvideo")) return "AVI video";
    return "video";
  }
  return "other";
}

/* Encode a string with file path encoding.
   `str' specifies the string.
   The return value is the result string.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if when is no longer in use. */
char *pathencode(const char *str)
{
  assert(str);
  int len = strlen(str);
  char *res = tcmalloc(len * 3 + 1);
  char *wp = res;
  while(true){
    int c = *(unsigned char *)str;
    if(c == '\0'){
      break;
    } else if(c == ' '){
      *(wp++) = '+';
    } else if(c < ' ' || c == '%' || c == '/' || c == 0x7f){
      wp += sprintf(wp, "%%%02X", c);
    } else {
      *(wp++) = c;
    }
    str++;
  }
  *wp = '\0';
  return res;
}

/* Trim invalid characters of an XML string.
   `str' specifies the string.
   The return value is the pointer to the given string. */
char *trimxmlchars(char *str)
{
  assert(str);
  const unsigned char *rp = (unsigned char *)str;
  unsigned char *wp = (unsigned char *)str;
  while (*rp != '\0') {
    int c = 0;
    int len = 0;
    if(*rp < 0x80){
      c = *rp;
      len = 1;
    } else if(*rp < 0xe0){
      if(rp[1] >= 0x80){
        c = ((rp[0] & 0x1f) << 6) | (rp[1] & 0x3f);
        len = 2;
      }
    } else if(*rp < 0xf0){
      if(rp[1] >= 0x80 && rp[2] >= 0x80){
        c = ((rp[0] & 0xf) << 12) | ((rp[1] & 0x3f) << 6) | (rp[2] & 0x3f);
        len = 3;
      }
    }
    if(len < 1) break;
    if(c == 0x0A || c == 0x0D || (c >= 0x20 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFD) ||
       (c >= 0x10000 && c <= 0x10FFFF)) {
      while (len-- > 0) {
        *(wp++) = *(rp++);
      }
    } else {
      *(wp++) = '?';
      rp += len;
    }
  }
  *wp = '\0';
  return str;
}

/* Store an article into the database.
   `tdb' specifies the database object.
   `id' specifies the ID number of the article.  If it is not more than 0, the auto-increment ID
   is assigned.
   `cols' specifies a map object containing columns.
   If successful, the return value is true, else, it is false. */
bool dbputart(TCTDB *tdb, int64_t id, TCMAP *cols)
{
  assert(tdb && cols);
  if(id < 1){
    id = tctdbgenuid(tdb);
    if(id < 1) return false;
  }
  const char *name = tcmapget2(cols, "name");
  if(!name || *name == '\0'){
    tctdbsetecode(tdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return false;
  }
  bool err = false;
  tcmapout2(cols, "id");
  int msiz = tcmapmsiz(cols);
  TCXSTR *wiki = tcxstrnew3(msiz + 1);
  wikidump(wiki, cols);
  TCMAP *ncols = tcmapnew2(TINYBNUM);
  wikiload(ncols, tcxstrptr(wiki));
  char pkbuf[NUMBUFSIZ];
  int pksiz = sprintf(pkbuf, "%lld", (long long)id);
  if(tctdbtranbegin(tdb)){
    if(tctdbput(tdb, pkbuf, pksiz, ncols)){
      if(tctdbtrancommit(tdb)){
        tcmapput2(cols, "id", pkbuf);
      } else {
        err = true;
      }
    } else {
      err = true;
      tctdbtranabort(tdb);
    }
  } else {
    err = true;
  }
  tcmapdel(ncols);
  tcxstrdel(wiki);
  return !err;
}

/* Remove an article from the database.
   `tdb' specifies the database object.
   `id' specifies the ID number of the article.
   If successful, the return value is true, else, it is false. */
bool dboutart(TCTDB *tdb, int64_t id)
{
  assert(tdb && id > 0);
  bool err = false;
  char pkbuf[NUMBUFSIZ];
  int pksiz = sprintf(pkbuf, "%lld", (long long)id);
  if(tctdbtranbegin(tdb)){
    if(tctdbout(tdb, pkbuf, pksiz)){
      if(!tctdbtrancommit(tdb)) err = true;
    } else {
      err = true;
      tctdbtranabort(tdb);
    }
  } else {
    err = true;
  }
  return !err;
}

/* Retrieve an article of the database.
   `tdb' specifies the database object.
   `id' specifies the ID number.
   If successful, the return value is a map object of the columns.  `NULL' is returned if no
   article corresponds.
   Because the object of the return value is created with the function `tcmapnew', it should be
   deleted with the function `tcmapdel' when it is no longer in use. */
TCMAP *dbgetart(TCTDB *tdb, int64_t id)
{
  assert(tdb && id > 0);
  char pkbuf[NUMBUFSIZ];
  int pksiz = sprintf(pkbuf, "%lld", (long long)id);
  return tctdbget(tdb, pkbuf, pksiz);
}

/* Generate the hash value of a user password.
   `pass' specifies the password string.
   `sal' specifies the salt string.
   `buf' specifies the result buffer whose size should be equal to or more than 48 bytes. */
void passwordhash(const char *pass, const char *salt, char *buf)
{
  assert(pass && salt && buf);
  TCXSTR *xstr = tcxstrnew();
  if(*salt != 0) tcxstrprintf(xstr, "%s:", salt);
  tcxstrcat2(xstr, pass);
  tcmd5hash(tcxstrptr(xstr), tcxstrsize(xstr), buf);
  tcxstrdel(xstr);
}

/* Check whether the name of a user is valid.
   `name' specifies the name of the user.
   The return value is true if the name is valid, else, it is false. */
bool checkusername(const char *name)
{
  assert(name);
  if(*name == '\0') return false;
  while(true){
    int c = *(name++);
    if(c == '\0') return true;
    if(c >= 'a' && c <= 'z') continue;
    if(c >= 'A' && c <= 'Z') continue;
    if(c >= '0' && c <= '9') continue;
    if(strchr("_-.", c)) continue;
    break;
  }
  return false;
}

/* Check whether an article is frozen.
   `cols' specifies a map object containing columns.
   The return value is true if the article is frozen, else, it is false. */
bool checkfrozen(TCMAP *cols)
{
  assert(cols);
  const char *tags = tcmapget2(cols, "tags");
  if(!tags) return false;
  for(const char *rp = tags; *rp != '\0'; rp++){
    if((*rp == '*') && (rp == tags || rp[-1] == ' ' || rp[-1] == ',') &&
       (rp[1] == '\0' || rp[1] == ' ' || rp[1] == ',')) return true;
  }
  return false;
}

/*
 * promenade.c
 */
#define SALTNAME       "[salt]"          // dummy user name of the salt
#define RIDDLENAME     "[riddle]"        // dummy user name of the riddle
#define ADMINNAME      "admin"           // user name of the administrator

typedef enum {
	HTTP_STATUS_WAITING = 0,
	HTTP_STATUS_200 = 200,
	HTTP_STATUS_304 = 304,	/* not-modified */
	HTTP_STATUS_500 = 500	/* system error */
} http_status;
typedef struct {                         // type of structure for a record
  int64_t id;                            // ID of the article
  int64_t date;                          // date
  const char *owner;                     // owner
  const char *text;                      // text
} COMMENT;

/* global variables */
time_t g_starttime = 0;                  // start time of the process
TCMPOOL *g_mpool = NULL;                 // global memory pool
TCTMPL *g_tmpl = NULL;                   // template serializer
TCMAP *g_users = NULL;                   // user list
const char *g_scriptname;                // script name
const char *g_scriptprefix;              // script prefix
const char *g_scriptpath;                // script path
const char *g_docroot;                   // document root
const char *g_database;                  // path of the database file
const char *g_password;                  // path of the password file
const char *g_upload;                    // path of the upload directory
const char *g_uploadpub;                 // public path of the upload directory
int64_t g_recvmax;                       // maximum size of received data
const char *g_mimerule;                  // mime dicision rule
const char *g_title;                     // site title
int g_searchnum;                         // number of articles in a search page
int g_listnum;                           // number of articles in a list page
int g_feedlistnum;                       // number of articles in a RSS feed
int g_filenum;                           // number of files in a file list page
int g_sidebarnum;                        // number of items in the side bar
const char *g_commentmode;               // comment mode
const char *g_updatecmd;                 // path of the update command
int g_sessionlife;                       // lifetime of each session
const char *g_frontpage;                 // name of the front page

typedef struct {
	TCMPOOL	*mpool;
	printbuf *pbuf;
	TCMAP *params, *vars;
	time_t start_tm;
	int in_len, in_cnt, out_len, out_cnt, proc_cnt;
	char *in_buf;
	const char *out_type, *out_buf;
} user_state;

typedef int (*user_proto)(struct shttpd_arg *);

#define TCTYPRFXLIST   "[list]\0:"       // type prefix for a list object
#define TCTYPRFXMAP    "[map]\0:"        // type prefix for a list object

static void dump_tclist(const char* prfx, TCLIST* list);
static void dump_tcmap(const char* prfx, TCMAP* map);

static void dump_tcval(const char* prfx, const char* vp, int siz)
{
	if (siz == sizeof(TCTYPRFXMAP)-1+sizeof(TCMAP*) &&
		!memcmp(vp, TCTYPRFXMAP, sizeof(TCTYPRFXMAP)-1)) {
		TCMAP* map = NULL;
 		memcpy(&map, vp+sizeof(TCTYPRFXMAP)-1, sizeof(map));
		// MY_DEBUG("%s() %p\n", __func__, map);
		dump_tcmap(prfx, map);
	} else if (siz == sizeof(TCTYPRFXLIST)-1+sizeof(TCLIST*) &&
		!memcmp(vp, TCTYPRFXLIST, sizeof(TCTYPRFXLIST)-1)) {
		TCLIST* list = NULL;
 		memcpy(&list, vp+sizeof(TCTYPRFXLIST)-1, sizeof(list));
		// MY_DEBUG("%s() %p\n", __func__, list);
		dump_tclist(prfx, list);
	} else {
		MY_DEBUG("WIERD:%s:", prfx);
		for (const char* cp = vp+strlen(vp)+1; cp < vp+siz; cp += strlen(cp)+1)
			printf("%s,", cp);
		printf(".\n");
	}
}

static void dump_tclist(const char* prfx, TCLIST* list)
{
	char buf[BUFSIZ];

	assert(list);
	int ln = tclistnum(list);
	for (int i = 0; i < ln; i++) {
		int siz;
		const char *vp = (const char*)tclistval(list, i, &siz);
		sprintf(buf, "%s[%d]", prfx, i);
		MY_DEBUG("%s:%d:%s.\n", buf, siz, vp);
		if (siz <= strlen(vp))
			continue;
		dump_tcval(buf, vp, siz);
	}
}

static void dump_tcmap(const char* prfx, TCMAP* map)
{
	const char *rp, *vp;
	int siz;

	assert(map);
	tcmapiterinit(map);
	while ((rp = tcmapiternext2(map)) != NULL) {
		// wkliang:20110608: don't work -> tcmapout2(state->params, rp);
		vp = tcmapiterval(rp, &siz);
		MY_DEBUG("%s:%s:%d:%.*s.\n", prfx, rp, siz, siz<128?siz:128, vp);
		if (siz <= strlen(vp))
			continue;
		dump_tcval(rp, vp, siz);
	}
}

static inline void vars_add(struct shttpd_arg *arg, const char *key, const char *val)
{
//	assert(state->vars && key && val);
//	if (tcmapget4(state->vars, key, NULL))
		tcmapputcat2(((user_state*)(arg->state))->vars, key, val);
//	else
//		tcmapput2(state->vars, key, val);
}

static void vars_setup(struct shttpd_arg *arg)
{
	user_state *state = arg->state;
	TCMPOOL *mpool = state->mpool;
	TCMAP *vars = state->vars;
	const char *cp;

  	tcmapput2(vars, "tpversion", TPVERSION);
  	tcmapput2(vars, "documentroot", g_docroot);
	tcmapput2(vars, "scriptname", g_scriptname);
	tcmapput2(vars, "scriptprefix", g_scriptprefix);
	tcmapput2(vars, "scriptpath", g_scriptpath);
	if (g_users) tcmapputmap(vars, "users", g_users);
	if (g_uploadpub) tcmapput2(vars, "uploadpub", g_uploadpub);

	cp = shttpd_getenv(arg, "REMOTE_HOST");
	tcmapput2(vars, "remotehost", cp ? cp : "");

	cp = shttpd_getenv(arg, "HTTP_USER-AGENT");
  	tcmapput2(vars, "useragent", cp ? cp : "");

  	cp = shttpd_getenv(arg, "HTTP_REFERER");
  	tcmapput2(vars, "referrer", cp ? cp : "");

	const char *p_userlang = "";
	cp = shttpd_getenv(arg, "HTTP_ACCEPT-LANGUAGE");
	if (cp) {
		char *lang = tcmpoolpushptr(mpool, tcstrdup(cp));
		char *pv = strchr(lang, ',');
		if(pv) *pv = '\0';
		pv = strchr(lang, ';');
		if(pv) *pv = '\0';
		pv = strchr(lang, '-');
		if(pv) *pv = '\0';
		tcstrtrim(lang);
		p_userlang = lang;
	}
  	tcmapput2(vars, "userlang", p_userlang);

	cp = shttpd_getenv(arg, "HTTP_HOST");
	if (!cp)
		cp = shttpd_getenv(arg, "SERVER_NAME");
	const char *p_hostname = cp ? cp : "";
	tcmapput2(vars, "hostname", p_hostname);

	cp = shttpd_getenv(arg, "SSL_PROTOCOL_VERSION");
	const char *p_scheme = (cp && *cp != '\0') ? "https" : "http";
  	tcmapput2(vars, "scheme", p_scheme);

  	const char *p_scripturl = tcmpoolpushptr(mpool,
		tcsprintf("%s://%s%s", p_scheme, p_hostname, g_scriptname));
	tcmapput2(vars, "scripturl", p_scripturl);
}

static inline const char* params_get(struct shttpd_arg *arg, const char *kstr, const char *dval)
{
	int vsiz;
	const char *vbuf = tcmapget(((user_state*)(arg->state))->params, kstr, strlen(kstr), &vsiz);
	if (!vbuf)
		return dval;
	const char *vptr = vbuf;
	while (vptr+strlen(vptr)+1 < vbuf+vsiz)
		vptr += strlen(vptr)+1;
	while (*vptr > '\0' && *vptr <= ' ') // trim leading white
		vptr++;
	return vptr;
}

/* show the error page */
static void showerror(struct shttpd_arg *arg, int code, const char *msg)
{
	MY_DEBUG("%s(%d, %s)\n", __func__, code, msg);
	shttpd_printf(arg, "HTTP/1.1 %03d XD\r\n", code);
	switch(code){
	case 304:
		shttpd_printf(arg, "Status: 304 Not Modified\r\n");
		break;
	case 400:
		shttpd_printf(arg, "Status: 400 Bad Request\r\n");
		break;
	case 404:
		shttpd_printf(arg, "Status: 404 File Not Found\r\n");
		break;
	case 413:
		shttpd_printf(arg, "Status: 413 Request Entity Too Large\r\n");
		break;
	case 500:
		shttpd_printf(arg, "Status: 500 Internal Server Error\r\n");
		break;
	default:
		shttpd_printf(arg, "Status: %03d Error\r\n", code);
		break;
	}
	shttpd_printf(arg, "Content-Type: text/plain; charset=UTF-8\r\n");
	shttpd_printf(arg, "Content-Length: %d\r\n\r\n", strlen(msg));
	shttpd_printf(arg, "%s", msg);
}

/* This callback function is used to show how to handle 404 error */
static void show_404(struct shttpd_arg *arg)
{
	showerror(arg, 404, "Oops. File not found! This is a custom error handler.");
	arg->flags |= SHTTPD_END_OF_OUTPUT;
}

/*
 * This callback function is attached to the wildcard URI "/users/.*"
 * It shows a greeting message and an actual URI requested by the user.
 */
static void show_users(struct shttpd_arg *arg)
{
	shttpd_printf(arg, "%s", "HTTP/1.1 200 OK\r\n");
	shttpd_printf(arg, "%s", "Content-Type: text/html\r\n\r\n");
	shttpd_printf(arg, "%s", "<html><body>");
	shttpd_printf(arg, "%s", "<h1>Hi. This is a wildcard uri handler"
	    "for the URI /users/*/ </h1>");
	shttpd_printf(arg, "<h2>URI: %s</h2></body></html>",
		shttpd_getenv(arg, "REQUEST_URI"));
	arg->flags |= SHTTPD_END_OF_OUTPUT;
}

/*
 * This callback function is attached to the "/secret" URI.
 * It shows simple text message, but in order to be shown, user must
 * authorized himself against the passwords file "passfile".
 */
static void show_secret(struct shttpd_arg *arg)
{
	shttpd_printf(arg, "%s", "HTTP/1.1 200 OK\r\n");
	shttpd_printf(arg, "%s", "Content-Type: text/html\r\n\r\n");
	shttpd_printf(arg, "%s", "<html><body>");
	shttpd_printf(arg, "%s", "<p>This is a protected page</body></html>");
	arg->flags |= SHTTPD_END_OF_OUTPUT;
}

/* read the password file */
static void readpasswd(void)
{
  if(!g_password) return;
  TCLIST *lines = tcreadfilelines(g_password);
  if(!lines) return;
  int lnum = tclistnum(lines);
  for(int i = 0; i < lnum; i++){
    const char *line = tclistval2(lines, i);
    const char *pv = strchr(line, ':');
    if(!pv) continue;
    tcmapputkeep(g_users, line, pv - line, pv + 1, strlen(pv + 1));
  }
  tclistdel(lines);
}

/* write the password file */
static bool writepasswd(void)
{
  if(!g_password) return false;
  bool err = false;
  TCXSTR *xstr = tcxstrnew();
  tcmapiterinit(g_users);
  const char *name;
  while((name = tcmapiternext2(g_users)) != NULL){
    const char *value = tcmapiterval2(name);
    tcxstrprintf(xstr, "%s:%s\n", name, value);
  }
  if(!tcwritefile(g_password, tcxstrptr(xstr), tcxstrsize(xstr))) err = true;
  tcxstrdel(xstr);
  return !err;
}

/* set a database error message */
static void setdberrmsg(TCLIST *emsgs, TCTDB *tdb, const char *msg)
{
  tclistprintf(emsgs, "[database error: %s] %s", tctdberrmsg(tctdbecode(tdb)), msg);
}

/* set the HTML data of an article */
static void setarthtml(TCMPOOL *mpool, TCMAP *cols, int64_t id, int bhl, bool tiny)
{
  tcmapprintf(cols, "id", "%lld", (long long)id);
  char idbuf[NUMBUFSIZ];
  sprintf(idbuf, "article%lld", (long long)id);
  char numbuf[NUMBUFSIZ];
  const char *rp = tcmapget2(cols, "cdate");
  if(rp){
    tcdatestrwww(tcstrmktime(rp), INT_MAX, numbuf);
    tcmapput2(cols, "cdate", numbuf);
    tcmapput2(cols, "cdatesimple", datestrsimple(numbuf));
  }
  rp = tcmapget2(cols, "mdate");
  if(rp){
    tcdatestrwww(tcstrmktime(rp), INT_MAX, numbuf);
    tcmapput2(cols, "mdate", numbuf);
    tcmapput2(cols, "mdatesimple", datestrsimple(numbuf));
  }
  rp = tcmapget2(cols, "xdate");
  if(rp){
    tcdatestrwww(tcstrmktime(rp), INT_MAX, numbuf);
    tcmapput2(cols, "xdate", numbuf);
    tcmapput2(cols, "xdatesimple", datestrsimple(numbuf));
  }
  rp = tcmapget2(cols, "tags");
  if(rp){
    TCLIST *tags = tcmpoolpushlist(mpool, tcstrsplit(rp, " ,"));
    int idx = 0;
    while(idx < tclistnum(tags)){
      rp = tclistval2(tags, idx);
      if(*rp != '\0'){
        idx++;
      } else {
        tcfree(tclistremove2(tags, idx));
      }
    }
    tcmapputlist(cols, "taglist", tags);
  }
  rp = tcmapget2(cols, "text");
  if(rp && *rp != '\0'){
    if(tiny){
      TCXSTR *xstr = tcmpoolxstrnew(mpool);
      wikitotext(xstr, rp);
      char *str = tcmpoolpushptr(mpool, tcmemdup(tcxstrptr(xstr), tcxstrsize(xstr)));
      tcstrutfnorm(str, TCUNSPACE);
      tcstrcututf(str, 256);
      tcmapput2(cols, "texttiny", str);
    } else {
      TCXSTR *xstr = tcmpoolxstrnew(mpool);
      wikitohtml(xstr, rp, idbuf, g_scriptname, bhl + 1, g_uploadpub);
      if(tcxstrsize(xstr) > 0) tcmapput(cols, "texthtml", 8, tcxstrptr(xstr), tcxstrsize(xstr));
    }
  }
  rp = tcmapget2(cols, "comments");
  if(rp && *rp != '\0'){
    TCLIST *lines = tcmpoolpushlist(mpool, tcstrsplit(rp, "\n"));
    int cnum = tclistnum(lines);
    tcmapprintf(cols, "comnum", "%d", cnum);
    TCLIST *comments = tcmpoolpushlist(mpool, tclistnew2(cnum));
    int cnt = 0;
    for(int i = 0; i < cnum; i++){
      const char *rp = tclistval2(lines, i);
      char *co = strchr(rp, '|');
      if(co){
        *(co++) = '\0';
        char *ct = strchr(co, '|');
        if(ct){
          *(ct++) = '\0';
          TCMAP *comment = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
          char numbuf[NUMBUFSIZ];
          tcdatestrwww(tcatoi(rp), INT_MAX, numbuf);
          cnt++;
          tcmapprintf(comment, "cnt", "%d", cnt);
          tcmapput2(comment, "date", numbuf);
          tcmapput2(comment, "datesimple", datestrsimple(numbuf));
          tcmapput2(comment, "owner", co);
          tcmapput2(comment, "text", ct);
          TCXSTR *xstr = tcmpoolxstrnew(mpool);
          wikitohtmlinline(xstr, ct, g_scriptname, g_uploadpub);
          tcmapput(comment, "texthtml", 8, tcxstrptr(xstr), tcxstrsize(xstr));
          tclistpushmap(comments, comment);
        }
      }
    }
    if(tclistnum(comments) > 0) tcmapputlist(cols, "comments", comments);
    tcmapprintf(cols, "comnum", "%d", tclistnum(comments));
  }
}

/* get the range of a date expression */
static void getdaterange(const char *expr, int64_t *lowerp, int64_t *upperp)
{
  while(*expr == ' '){
    expr++;
  }
  unsigned int year = 0;
  for(int i = 0; i < 4 && *expr >= '0' && *expr <= '9'; i++){
    year = year * 10 + *expr - '0';
    expr++;
  }
  if(*expr == '-' || *expr == '/') expr++;
  unsigned int month = 0;
  for(int i = 0; i < 2 && *expr >= '0' && *expr <= '9'; i++){
    month = month * 10 + *expr - '0';
    expr++;
  }
  if(*expr == '-' || *expr == '/') expr++;
  unsigned int day = 0;
  for(int i = 0; i < 2 && *expr >= '0' && *expr <= '9'; i++){
    day = day * 10 + *expr - '0';
    expr++;
  }
  int lag = tcjetlag() / 3600;
  int64_t lower, upper;
  char numbuf[NUMBUFSIZ*2];
  if(day > 0){
    sprintf(numbuf, "%04u-%02u-%02uT00:00:00%+03d:00", year, month, day, lag);
    lower = tcstrmktime(numbuf);
    upper = lower + 60 * 60 * 24 - 1;
  } else if(month > 0){
    sprintf(numbuf, "%04u-%02u-01T00:00:00%+03d:00", year, month, lag);
    lower = tcstrmktime(numbuf);
    month++;
    if(month > 12){
      year++;
      month = 1;
    }
    sprintf(numbuf, "%04u-%02u-01T00:00:00%+03d:00", year, month, lag);
    upper = tcstrmktime(numbuf) - 1;
  } else if(year > 0){
    sprintf(numbuf, "%04u-01-01T00:00:00%+03d:00", year, lag);
    lower = tcstrmktime(numbuf);
    year++;
    sprintf(numbuf, "%04u-01-01T00:00:00%+03d:00", year, lag);
    upper = tcstrmktime(numbuf) - 1;
  } else {
    lower = INT64_MIN / 2;
    upper = INT64_MAX / 2;
  }
  *lowerp = lower;
  *upperp = upper;
}

/* search for articles */
static TCLIST *searcharts(TCMPOOL *mpool, TCTDB *tdb, const char *cond, const char *expr,
                          const char *order, int max, int skip, bool ls)
{
  TDBQRY *qrys[8];
  int qnum = 0;
  if(!cond) cond = "";
  if(!expr) expr = "";
  if(*expr == '\0'){
    qrys[qnum++] = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
  } else if(!strcmp(cond, "main")){
    const char *names[] = { "name", "text", NULL };
    for(int i = 0; names[i] != NULL; i++){
      TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
      tctdbqryaddcond(qry, names[i], TDBQCFTSEX, expr);
      qrys[qnum++] = qry;
    }
  } else if(!strcmp(cond, "name")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "name", TDBQCSTREQ, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "namebw")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "name", TDBQCSTRBW, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "namefts")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "name", TDBQCFTSEX, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "cdate")){
    int64_t lower, upper;
    getdaterange(expr, &lower, &upper);
    char numbuf[NUMBUFSIZ*2];
    sprintf(numbuf, "%lld,%lld", (long long)lower, (long long)upper);
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "cdate", TDBQCNUMBT, numbuf);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "mdate")){
    int64_t lower, upper;
    getdaterange(expr, &lower, &upper);
    char numbuf[NUMBUFSIZ*2];
    sprintf(numbuf, "%lld,%lld", (long long)lower, (long long)upper);
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "mdate", TDBQCNUMBT, numbuf);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "xdate")){
    int64_t lower, upper;
    getdaterange(expr, &lower, &upper);
    char numbuf[NUMBUFSIZ*2];
    sprintf(numbuf, "%lld,%lld", (long long)lower, (long long)upper);
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "xdate", TDBQCNUMBT, numbuf);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "owner")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "owner", TDBQCSTREQ, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "ownerbw")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "", TDBQCSTRBW, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "ownerfts")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "", TDBQCFTSEX, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "tags")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "tags", TDBQCSTRAND, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "tagsor")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "tags", TDBQCSTROR, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "tagsfts")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "tags", TDBQCFTSEX, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "text")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "text", TDBQCFTSEX, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "any")){
    const char *names[] = { "name", "owner", "tags", "text", NULL };
    for(int i = 0; names[i] != NULL; i++){
      TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
      tctdbqryaddcond(qry, names[i], TDBQCFTSEX, expr);
      qrys[qnum++] = qry;
    }
  } else {
    qrys[qnum++] = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
  }
  if(!order) order = "";
  const char *oname = "cdate";
  int otype = TDBQONUMDESC;
  if(!strcmp(order, "_cdate")){
    oname = "cdate";
    otype = TDBQONUMASC;
  } else if(!strcmp(order, "mdate")){
    oname = "mdate";
    otype = TDBQONUMDESC;
  } else if(!strcmp(order, "_mdate")){
    oname = "mdate";
    otype = TDBQONUMASC;
  } else if(!strcmp(order, "xdate")){
    oname = "xdate";
    otype = TDBQONUMDESC;
  } else if(!strcmp(order, "_xdate")){
    oname = "xdate";
    otype = TDBQONUMASC;
  }
  for(int i = 0; i < qnum; i++){
    if(ls) tctdbqryaddcond(qrys[i], "tags", TDBQCSTROR | TDBQCNEGATE, "?");
    tctdbqrysetorder(qrys[i], oname, otype);
  }
  for(int i = 0; i < qnum; i++){
    tctdbqrysetlimit(qrys[i], max, skip);
  }
  TCLIST *res = tcmpoolpushlist(mpool, tctdbmetasearch(qrys, qnum, TDBMSUNION));
  return res;
}

/* store a file */
static bool putfile(TCMPOOL *mpool, const char *path, const char *name,
                    const char *ptr, int size)
{
  if(*path != '\0'){
    if(strchr(path, '/')) return false;
  } else {
    char *enc = pathencode(name);
    path = tcmpoolpushptr(mpool, tcsprintf("%lld-%s", (long long)tctime(), enc));
    tcfree(enc);
  }
  path = tcmpoolpushptr(mpool, tcsprintf("%s/%s", g_upload, path));
  return tcwritefile(path, ptr, size);
}

/* remove a file */
static bool outfile(TCMPOOL *mpool, const char *path)
{
  if(*path == '\0' || strchr(path, '/')) return false;
  path = tcmpoolpushptr(mpool, tcsprintf("%s/%s", g_upload, path));
  return remove(path) == 0;
}

/* search for files */
static TCLIST *searchfiles(TCMPOOL *mpool, const char *expr, const char *order,
                           int max, int skip, bool thum)
{
  TCLIST *paths = tcmpoolpushlist(mpool, tcreaddir(g_upload));
  if(!paths) return tcmpoollistnew(mpool);
  tclistsort(paths);
  if(strcmp(order, "_cdate")) tclistinvert(paths);
  int pnum = tclistnum(paths);
  TCLIST *files = tcmpoolpushlist(mpool, tclistnew2(pnum));
  for(int i = 0; i < pnum && tclistnum(files) < max; i++){
    const char *path = tclistval2(paths, i);
    int64_t date = tcatoi(path);
    const char *pv = strchr(path, '-');
    if(date < 1 || !pv) continue;
    pv++;
    int nsiz;
    char *name = tcmpoolpushptr(mpool, tcurldecode(pv, &nsiz));
    if(*expr != '\0' && !strstr(name, expr)){
      tcmpoolpop(mpool, true);
      continue;
    }
    if(--skip >= 0){
      tcmpoolpop(mpool, true);
      continue;
    }
    char lpath[8192];
    snprintf(lpath, sizeof(lpath) - 1, "%s/%s", g_upload, path);
    lpath[sizeof(lpath)-1] = '\0';
    int64_t size;
    if(!tcstatfile(lpath, NULL, &size, NULL)) size = 0;
    char numbuf[NUMBUFSIZ];
    tcdatestrwww(date, INT_MAX, numbuf);
    TCMAP *file = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
    tcmapput2(file, "path", path);
    tcmapput2(file, "name", name);
    tcmapput2(file, "date", datestrsimple(numbuf));
    tcmapprintf(file, "size", "%lld", (long long)size);
    const char *type = mimetype(name);
    if(!type) type = "application/octet-stream";
    tcmapput2(file, "type", type);
    tcmapput2(file, "typename", mimetypename(type));
    if(thum && tcstrfwm(type, "image/") && !strchr(type, '+'))
      tcmapput2(file, "thumnail", "true");
    tclistpushmap(files, file);
  }
  return files;
}

/* compare two comments by date */
static int comparecomments(const TCLISTDATUM *a, const TCLISTDATUM *b)
{
  COMMENT *coma = (COMMENT *)a->ptr;
  COMMENT *comb = (COMMENT *)b->ptr;
  if(coma->date > comb->date) return -1;
  if(coma->date < comb->date) return 1;
  if(coma->id > comb->id) return -1;
  if(coma->id < comb->id) return 1;
  return strcmp(coma->owner, comb->owner);
}

/* process the update command */
static bool doupdatecmd(TCMPOOL *mpool, const char *mode, const char *baseurl, const char *user,
                        double now, int64_t id, TCMAP *ncols, TCMAP *ocols)
{
  const char *base = g_upload;
  if(!base || *base == '\0') base = P_tmpdir;
  if(!base || *base == '\0') base = "/tmp";
  int64_t ts = now * 1000000;
  bool err = false;

  TCXSTR *nbuf = tcmpoolxstrnew(mpool);
  if(ncols){
    tcmapprintf(ncols, "id", "%lld", (long long)id);
    wikidump(nbuf, ncols);
  }
  char *npath = tcmpoolpushptr(mpool, tcsprintf("%s/tmp-%lld-%lld-%s-new.tpw", base,
                                                (long long)id, (long long)ts, mode));
  if(!tcwritefile(npath, tcxstrptr(nbuf), tcxstrsize(nbuf))) err = true;

  TCXSTR *obuf = tcmpoolxstrnew(mpool);
  if(ocols){
    tcmapprintf(ocols, "id", "%lld", (long long)id);
    wikidump(obuf, ocols);
  }
  char *opath = tcmpoolpushptr(mpool, tcsprintf("%s/tmp-%lld-%lld-%s-old.tpw", base,
                                                (long long)id, (long long)ts, mode));
  if(!tcwritefile(opath, tcxstrptr(obuf), tcxstrsize(obuf))) err = true;

  char idbuf[NUMBUFSIZ];
  sprintf(idbuf, "%lld", (long long)id);
  char tsbuf[NUMBUFSIZ];
  sprintf(tsbuf, "%lld", (long long)ts);
  const char *args[16];
  int anum = 0;
  args[anum++] = g_updatecmd;
  args[anum++] = mode;
  args[anum++] = idbuf;
  args[anum++] = npath;
  args[anum++] = opath;
  args[anum++] = tsbuf;
  args[anum++] = user;
  args[anum++] = baseurl;
  if(tcsystem(args, anum) != 0) err = true;
  remove(opath);
  remove(npath);
  return !err;
}

/* process each session */
static int dosession(struct shttpd_arg *arg)
{
	user_state *state = arg->state;
	TCMPOOL* mpool = state->mpool;
	TCMAP *vars = state->vars;

	vars_setup(arg);
  // download a file
  const char *rp = shttpd_getenv(arg, "HTTP_IF-MODIFIED-SINCE");
  int64_t p_ifmod = rp ? tcstrmktime(rp) : 0;

  // prepare session-scope variables
  TCLIST *emsgs = tcmpoollistnew(mpool);
	double now = tctime();
	bool post = (state->in_len == 0) ? false : true;
  const char *p_user = params_get(arg, "user", "");
  const char *p_pass = params_get(arg, "pass", "");
  const char *p_format = params_get(arg, "format", "");
  const char *p_act = params_get(arg, "act", "");
  int64_t p_id = tcatoi(params_get(arg, "id", ""));
  const char *p_name = params_get(arg, "name", "");
  const char *p_order = params_get(arg, "order", "");
  const char *p_adjust = params_get(arg, "adjust", "");
  const char *p_expr = params_get(arg, "expr", "");
  const char *p_cond = params_get(arg, "cond", "");
  int p_page = tclmax(tcatoi(params_get(arg, "page", "")), 1);
  const char *p_wiki = params_get(arg, "wiki", "");
  bool p_mts = *params_get(arg, "mts", "") != '\0';
  const char *p_hash = params_get(arg, "hash", "");
  uint32_t p_seskey = tcatoi(params_get(arg, "seskey", ""));
  const char *p_comowner = params_get(arg, "comowner", "");
  const char *p_comtext = params_get(arg, "comtext", "");
  const char *p_ummode = params_get(arg, "ummode", "");
  const char *p_umname = params_get(arg, "umname", "");
  const char *p_uminfo = params_get(arg, "uminfo", "");
  const char *p_umpassone = params_get(arg, "umpassone", "");
  const char *p_umpasstwo = params_get(arg, "umpasstwo", "");
  const char *p_umridque = params_get(arg, "umridque", "");
  const char *p_umridans = params_get(arg, "umridans", "");
  const char *p_fmmode = params_get(arg, "fmmode", "");
  const char *p_fmpath = params_get(arg, "fmpath", "");
  const char *p_fmname = params_get(arg, "fmname", "");
  int p_fmfilesiz;
  const char *p_fmfilebuf = tcmapget(state->params, "fmfile", 6, &p_fmfilesiz);
  const char *p_fmfilename = params_get(arg, "fmfile_filename", "");
  bool p_fmthum = *params_get(arg, "fmthum", "") != '\0';
  bool p_confirm = *params_get(arg, "confirm", "") != '\0';

	const char *p_referrer = tcmapget2(vars, "referrer");
	const char *p_scripturl = tcmapget2(vars, "scripturl");
  if(*p_format == '\0'){
    if(!strcmp(g_mimerule, "xhtml")) {
      p_format = "xhtml";
    } else if(!strcmp(g_mimerule, "html")) {
      p_format = "html";
    } else {
      rp = shttpd_getenv(arg, "HTTP_ACCEPT");
      if(rp && strstr(rp, "application/xhtml+xml")) p_format = "xhtml";
    }
  }
  // perform authentication
  bool auth = true;
  const char *userinfo = NULL;
  uint32_t seskey = 0;
  const char *authcookie = NULL;
  const char *ridque = "";
  const char *ridans = "";
  const char *ridcookie = NULL;
	MY_DEBUG("%s,%s.\n", shttpd_getenv(arg,"AUTH_TYPE"), shttpd_getenv(arg,"REMOTE_USER"));
  rp = shttpd_getenv(arg, "AUTH_TYPE");
  if(rp && (!tcstricmp(rp, "Basic") || !tcstricmp(rp, "Digest")) &&
     (rp = shttpd_getenv(arg, "REMOTE_USER")) != NULL) {
    p_user = rp;
    userinfo = "";
    tcmapput2(vars, "basicauth", "true");
  } else if (g_users) {
    auth = false;
    const char *salt = tcmapget4(g_users, SALTNAME, "");
    int saltsiz = strlen(salt);
    bool cont = false;
    if (*p_user == '\0') {
      int authsiz;
      const char *authbuf = tcmapget(state->params, "auth", 4, &authsiz);
      if(authbuf && authsiz > 0){
        char *token = tcmpoolmalloc(mpool, authsiz + 1);
        tcarccipher(authbuf, authsiz, salt, saltsiz, token);
        token[authsiz] = '\0';
	// wkliang:20120612 token=tokyopromenade:wkliang:1q2w3e4r:261997753:1307875513.
	MY_DEBUG("token=%s.\n", token);
        TCLIST *elems = tcmpoolpushlist(mpool, tcstrsplit(token, ":"));
        if(tclistnum(elems) >= 4 && !strcmp(tclistval2(elems, 0), salt)){
          seskey = tcatoi(tclistval2(elems, 3));
          if(seskey > 0){
            p_user = tclistval2(elems, 1);
            p_pass = tclistval2(elems, 2);
            cont = true;
          }
        }
      }
    }
    if (*p_user != '\0') {
      rp = tcmapget2(g_users, p_user);
      if (rp) {
        char *hash = tcmpoolpushptr(mpool, tcstrdup(rp));
        char *pv = strchr(hash, ':');
        if(pv) *(pv++) = '\0';
        char numbuf[NUMBUFSIZ];
        passwordhash(p_pass, salt, numbuf);
	MY_DEBUG("%s:%s:%s:%s:%s.\n", p_user, p_pass, salt, hash, numbuf);
        if(!strcmp(hash, numbuf)){
          auth = true;
          userinfo = pv ? pv : "";
          if(seskey < 1){
            uint32_t seed = 19780211;
            for(rp = p_pass; *rp != '\0'; rp++){
              seed = seed * 31 + *(unsigned char *)rp;
            }
            double integ;
            double fract = modf(now, &integ) * (1ULL << 31);
            seskey = (((uint32_t)integ + (uint32_t)fract) ^ (seed << 8)) & INT32_MAX;
            if(seskey < 1) seskey = INT32_MAX;
          }
          int tsiz = strlen(p_user) + strlen(p_pass) + saltsiz + NUMBUFSIZ * 2;
          char token[tsiz];	// wkliang:20130105: could it be written in this way?!
          tsiz = sprintf(token, "%s:%s:%s:%u:%lld",
                         salt, p_user, p_pass, (unsigned int)seskey, (long long)now);
          tcmd5hash(token, tsiz, numbuf);
          sprintf(token + tsiz, ":%s", numbuf);	// tricky!?
          tcarccipher(token, tsiz, salt, saltsiz, token);
          if(!cont){
            authcookie = tcmpoolpushptr(mpool, tcurlencode(token, tsiz));
            p_seskey = seskey;
          }
        }
      }
    }
    rp = tcmapget2(g_users, RIDDLENAME);
    if(rp){
      const char *pv = strstr(rp, ":");
      if(pv){
        ridque = tcmpoolpushptr(mpool, tcstrdup(pv + 1));
        ridans = tcmpoolpushptr(mpool, tcmemdup(rp, pv - rp));
      }
    }
    if(*ridans != '\0'){
      const char *ridbuf = tcmapget2(state->params, "riddle");
      if((ridbuf && !tcstricmp(ridbuf, ridans)) || !tcstricmp(p_umridans, ridans))
        ridcookie = ridans;
    }
  }
  if(!strcmp(p_act, "logout")){
    p_user = "";
    p_pass = "";
    auth = false;
    seskey = 0;
    authcookie = "";
  }
  bool admin = auth && !strcmp(p_user, ADMINNAME);
  bool cancom = false;
  if(!strcmp(g_commentmode, "all") || (!strcmp(g_commentmode, "login") && auth) ||
     (!strcmp(g_commentmode, "riddle") && (ridcookie || auth))) cancom = true;
// open the database -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
  TCTDB *tdb = tcmpoolpush(mpool, tctdbnew(), (void (*)(void *))tctdbdel);
  int omode = TDBOREADER;
  if(!strcmp(p_act, "update") && auth && post) omode = TDBOWRITER;
  if(!strcmp(p_act, "comment") && cancom && post) omode = TDBOWRITER;
  if(post && auth && *p_referrer != '\0'){
    char *src = tcmpoolpushptr(mpool, tcstrdup(p_referrer));
    char *wp = strchr(src, '?');
    if(wp) *wp = '\0';
    if(strcmp(src, p_scripturl)){
      tclistprintf(emsgs, "Referrer is invalid (%s).", src);
      admin = false;
      post = false;
      omode = TDBOREADER;
    }
  }
	if(!tctdbopen(tdb, g_database, omode))
		setdberrmsg(emsgs, tdb, "Opening the database was failed.");
	int64_t mtime = tctdbmtime(tdb);
	if (mtime < 1) mtime = now;
// prepare the common query -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
  TCXSTR *comquery = tcmpoolxstrnew(mpool);
  if(*p_act != '\0') tcxstrprintf(comquery, "&act=%?", p_act);
  if(p_id > 0) tcxstrprintf(comquery, "&id=%lld", (long long)p_id);
  if(*p_name != '\0') tcxstrprintf(comquery, "&name=%?", p_name);
  if(*p_order != '\0') tcxstrprintf(comquery, "&order=%?", p_order);
  if(*p_expr != '\0') tcxstrprintf(comquery, "&expr=%?", p_expr);
  if(*p_cond != '\0') tcxstrprintf(comquery, "&cond=%?", p_cond);
  if(p_fmthum) tcxstrprintf(comquery, "&fmthum=on");
// save a comment -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
  if (!strcmp(p_act, "comment") && p_id > 0 && *p_comowner != '\0' && *p_comtext != '\0') {
    char *owner = tcmpoolpushptr(mpool, tcstrdup(p_comowner));
    tcstrsqzspc(owner);
    char *text = tcmpoolpushptr(mpool, tcstrdup(p_comtext));
    tcstrsqzspc(text);
    if(*owner != '\0' && *text != '\0'){
      if (!checkusername(p_comowner)) {
        tclistprintf(emsgs, "%s(%d): %s", __FILE__, __LINE__, "invalid user name.");
      } else {
        TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, p_id));
        if(cols){
          if(checkfrozen(cols) && !admin){
            tclistprintf(emsgs, "Frozen articles are not editable by normal users.");
          } else {
            TCMAP *ocols = *g_updatecmd != '\0' ? tcmpoolpushmap(mpool, tcmapdup(cols)) : NULL;
            TCXSTR *wiki = tcmpoolxstrnew(mpool);
            wikidump(wiki, cols);
            TCXSTR *line = tcmpoolxstrnew(mpool);
            tcxstrprintf(line, "%lld|%s|%s\n", (long long)now, owner, text);
            tcmapputcat(cols, "comments", 8, tcxstrptr(line), tcxstrsize(line));
            if(dbputart(tdb, p_id, cols)){
              if(*g_updatecmd != '\0' &&
                 !doupdatecmd(mpool, "comment", p_scripturl, p_user, now, p_id, cols, ocols))
                tclistprintf(emsgs, "The update command was failed.");
            } else {
              setdberrmsg(emsgs, tdb, "Storing the article was failed.");
            }
          }
        }
      }
    }
  }
// perform each view -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
  if (!strcmp(shttpd_getenv(arg,"REQUEST_URI"),"/")) {
	tcmapprintf(vars, "titletip", "goFirst.tw");
	tcmapput2(vars, "view", "chat");
  } else if (!strcmp(p_act, "login")) {	// login view
	tcmapprintf(vars, "titletip", "[login]");
	tcmapput2(vars, "view", "login");
  } else if (!strcmp(p_act, "logincheck") && !auth) {	// login view
	tcmapprintf(vars, "titletip", "[login]");
	tcmapput2(vars, "view", "login");
  } else if (!strcmp(p_act, "edit")) {	// edit view
    if(p_id > 0){
      TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, p_id));
      if(cols){
        if(checkfrozen(cols) && !admin){
          tclistprintf(emsgs, "Frozen articles are not editable by normal users.");
        } else {
          TCXSTR *wiki = tcmpoolxstrnew(mpool);
          wikidump(wiki, cols);
          tcmapprintf(cols, "id", "%lld", (long long)p_id);
          char numbuf[NUMBUFSIZ];
          tcmd5hash(tcxstrptr(wiki), tcxstrsize(wiki), numbuf);
          tcmapput2(cols, "hash", numbuf);
          tcmapput2(vars, "view", "edit");
          tcmapputmap(vars, "art", cols);
          tcmapput(vars, "wiki", 4, tcxstrptr(wiki), tcxstrsize(wiki));
        }
      } else {
        tcmapput2(vars, "view", "empty");
      }
      tcmapprintf(vars, "cond", "id:%lld", (long long)p_id);
    } else {
      tcmapprintf(vars, "titletip", "[edit]");
      tcmapput2(vars, "view", "edit");
      if(*p_name != '\0') tcmapput2(vars, "name", p_name);
      if(*p_user != '\0') tcmapput2(vars, "user", p_user);
      if(!strcmp(p_adjust, "front")) tcmapput2(vars, "tags", "*,?");
    }
  } else if (!strcmp(p_act, "preview")) { // preview view
    if(p_id > 0){
      TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, p_id));
      if(cols){
        if(checkfrozen(cols) && !admin){
          tclistprintf(emsgs, "Frozen articles are not editable by normal users.");
        } else {
          TCXSTR *wiki = tcmpoolxstrnew(mpool);
          wikidump(wiki, cols);
          char numbuf[NUMBUFSIZ];
          tcmd5hash(tcxstrptr(wiki), tcxstrsize(wiki), numbuf);
          if(!strcmp(numbuf, p_hash)){
            if(*p_wiki != '\0'){
              tcmapclear(cols);
              wikiload(cols, p_wiki);
              if(p_mts) tcmapprintf(cols, "mdate", "%lld", (long long)now);
              const char *name = tcmapget2(cols, "name");
              if(!name || *name == '\0'){
                tclistprintf(emsgs, "The name can not be empty.");
              } else if(checkfrozen(cols) && !admin){
                tclistprintf(emsgs, "The frozen tag is not available by normal users.");
              } else {
                setarthtml(mpool, cols, p_id, 0, false);
                tcmapprintf(vars, "titletip", "[preview]");
                tcmapput2(vars, "view", "preview");
                tcmapputmap(vars, "art", cols);
                tcmapput2(vars, "wiki", p_wiki);
                if(p_mts) tcmapput2(vars, "mts", "on");
                tcmapprintf(vars, "id", "%lld", (long long)p_id);
                tcmapput2(vars, "hash", p_hash);
              }
            } else {
              tcmapput2(vars, "view", "removecheck");
              tcmapputmap(vars, "art", cols);
              tcmapprintf(vars, "id", "%lld", (long long)p_id);
              tcmapput2(vars, "hash", p_hash);
            }
          } else {
            tcmapput2(vars, "view", "collision");
            tcmapput(vars, "wiki", 4, tcxstrptr(wiki), tcxstrsize(wiki));
            tcmapput2(vars, "yourwiki", p_wiki);
          }
        }
      } else {
        tcmapput2(vars, "view", "empty");
      }
      tcmapprintf(vars, "cond", "id:%lld", (long long)p_id);
    } else {
      TCMAP *cols = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
      wikiload(cols, p_wiki);
      if(p_mts){
        tcmapprintf(cols, "cdate", "%lld", (long long)now);
        tcmapprintf(cols, "mdate", "%lld", (long long)now);
      }
      const char *name = tcmapget2(cols, "name");
      if(!name || *name == '\0'){
        tclistprintf(emsgs, "The name can not be empty.");
        tcmapput2(vars, "view", "edit");
        tcmapput2(vars, "wiki", p_wiki);
      } else if(checkfrozen(cols)  && !admin){
        tclistprintf(emsgs, "The frozen tag is not available by normal users.");
        tcmapput2(vars, "view", "edit");
        tcmapput2(vars, "wiki", p_wiki);
      } else {
        setarthtml(mpool, cols, 0, 0, false);
        tcmapprintf(vars, "titletip", "[preview]");
        tcmapput2(vars, "view", "preview");
        tcmapputmap(vars, "art", cols);
        tcmapput2(vars, "wiki", p_wiki);
        if(p_mts) tcmapput2(vars, "mts", "on");
      }
    }
  } else if (!strcmp(p_act, "update")) { // update view
    if(seskey > 0 && p_seskey != seskey){
      tclistprintf(emsgs, "The session key is invalid (%u).", (unsigned int)p_seskey);
    } else if(p_id > 0){
      TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, p_id));
      if(cols){
        if(checkfrozen(cols) && !admin){
          tclistprintf(emsgs, "Frozen articles are not editable by normal users.");
        } else {
          TCXSTR *wiki = tcmpoolxstrnew(mpool);
          wikidump(wiki, cols);
          char numbuf[NUMBUFSIZ];
          tcmd5hash(tcxstrptr(wiki), tcxstrsize(wiki), numbuf);
          if(!strcmp(numbuf, p_hash)){
            TCMAP *ocols = *g_updatecmd != '\0' ? tcmpoolpushmap(mpool, tcmapdup(cols)) : NULL;
            if(*p_wiki != '\0'){
              tcmapclear(cols);
              wikiload(cols, p_wiki);
              if(p_mts) tcmapprintf(cols, "mdate", "%lld", (long long)now);
              const char *name = tcmapget2(cols, "name");
              if(!name || *name == '\0'){
                tclistprintf(emsgs, "The name can not be empty.");
              } else if(checkfrozen(cols) && !admin){
                tclistprintf(emsgs, "The frozen tag is not available by normal users.");
              } else if(dbputart(tdb, p_id, cols)){
                if(*g_updatecmd != '\0' &&
                   !doupdatecmd(mpool, "update", p_scripturl, p_user, now, p_id, cols, ocols))
                  tclistprintf(emsgs, "The update command was failed.");
                tcmapput2(vars, "view", "store");
                tcmapputmap(vars, "art", cols);
              } else {
                setdberrmsg(emsgs, tdb, "Storing the article was failed.");
              }
            } else {
              if(dboutart(tdb, p_id)){
                if(*g_updatecmd != '\0' &&
                   !doupdatecmd(mpool, "remove", p_scripturl, p_user, now, p_id, NULL, ocols))
                  tclistprintf(emsgs, "The update command was failed.");
                tcmapprintf(cols, "id", "%lld", (long long)p_id);
                tcmapput2(vars, "view", "remove");
                tcmapputmap(vars, "art", cols);
              } else {
                setdberrmsg(emsgs, tdb, "Removing the article was failed.");
              }
            }
          } else {
            tcmapput2(vars, "view", "collision");
            tcmapput(vars, "wiki", 4, tcxstrptr(wiki), tcxstrsize(wiki));
            tcmapput2(vars, "yourwiki", p_wiki);
          }
        }
      } else {
        tcmapput2(vars, "view", "empty");
      }
      tcmapprintf(vars, "cond", "id:%lld", (long long)p_id);
    } else {
      TCMAP *cols = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
      wikiload(cols, p_wiki);
      if(p_mts){
        tcmapprintf(cols, "cdate", "%lld", (long long)now);
        tcmapprintf(cols, "mdate", "%lld", (long long)now);
      }
      const char *name = tcmapget2(cols, "name");
      if(!name || *name == '\0'){
        tclistprintf(emsgs, "The name can not be empty.");
        tcmapput2(vars, "view", "edit");
        tcmapput2(vars, "wiki", p_wiki);
      } else if(dbputart(tdb, 0, cols)){
        rp = tcmapget2(cols, "id");
        int64_t nid = rp ? tcatoi(rp) : 0;
        if(*g_updatecmd != '\0' &&
           !doupdatecmd(mpool, "new", p_scripturl, p_user, now, nid, cols, NULL))
          tclistprintf(emsgs, "The update command was failed.");
        tcmapput2(vars, "view", "store");
        tcmapputmap(vars, "art", cols);
      } else {
        setdberrmsg(emsgs, tdb, "Storing the article was failed.");
      }
    }
  } else if (!strcmp(p_act, "users")) { // users view
	if (!g_users) {
		tclistprintf(emsgs, "The password file is missing.");
	} else if (!admin) {
		tclistprintf(emsgs, "The user management function is not available by normal users.");
	} else {
        if(post && p_umname != '\0'){
          if(seskey > 0 && p_seskey != seskey){
            tclistprintf(emsgs, "The session key is invalid (%u).", (unsigned int)p_seskey);
          } else if(!strcmp(p_ummode, "new")){
            if(tcmapget2(g_users, p_umname)){
              tclistprintf(emsgs, "The user already exists.");
            } else if(!checkusername(p_umname)){
              tclistprintf(emsgs, "%s(%d): %s", __FILE__, __LINE__, "invalid user name.");
            } else if(strcmp(p_umpassone, p_umpasstwo)){
              tclistprintf(emsgs, "The two passwords are different.");
            } else {
              const char *salt = tcmapget4(g_users, SALTNAME, "");
              char numbuf[NUMBUFSIZ];
              passwordhash(p_umpassone, salt, numbuf);
              tcmapprintf(g_users, p_umname, "%s:%s", numbuf, p_uminfo);
              if(writepasswd()){
                tcmapput2(vars, "newuser", p_umname);
              } else {
                tclistprintf(emsgs, "Storing the password file was failed.");
              }
            }
          } else if(!strcmp(p_ummode, "chpw")){
            const char *pass = tcmapget2(g_users, p_umname);
            if(!pass){
              tclistprintf(emsgs, "The user does not exist.");
            } else if(strcmp(p_umpassone, p_umpasstwo)){
              tclistprintf(emsgs, "The two passwords are different.");
            } else {
              char *str = tcmpoolpushptr(mpool, tcstrdup(pass));
              char *pv = strchr(str, ':');
              if(pv){
                *(pv++) = '\0';
              } else {
                pv = "";
              }
              const char *salt = tcmapget4(g_users, SALTNAME, "");
              char numbuf[NUMBUFSIZ];
              passwordhash(p_umpassone, salt, numbuf);
              tcmapprintf(g_users, p_umname, "%s:%s", numbuf, pv);
              if(writepasswd()){
                tcmapput2(vars, "chpwuser", p_umname);
                if(!strcmp(p_umname, p_user)){
                  p_user = "";
                  p_pass = "";
                  auth = false;
                  authcookie = "";
                  tcmapput2(vars, "tologin", p_umname);
                }
              } else {
                tclistprintf(emsgs, "Storing the password file was failed.");
              }
            }
          } else if(!strcmp(p_ummode, "del") && p_confirm){
            if(!tcmapget2(g_users, p_umname)){
              tclistprintf(emsgs, "The user does not exist.");
            } else {
              tcmapout2(g_users, p_umname);
              if(writepasswd()){
                tcmapput2(vars, "deluser", p_umname);
                if(!strcmp(p_umname, p_user)){
                  p_user = "";
                  p_pass = "";
                  auth = false;
                  authcookie = "";
                  tcmapput2(vars, "tologin", p_umname);
                }
              } else {
                tclistprintf(emsgs, "Storing the password file was failed.");
              }
            }
          } else if(!strcmp(p_ummode, "rid")){
            if(!checkusername(p_umridans)){
              tclistprintf(emsgs, "The answer is invalid.");
            } else {
              tcmapprintf(g_users, RIDDLENAME, "%s:%s", p_umridans, p_umridque);
              if(writepasswd()){
                tcmapput2(vars, "chrid", p_umname);
                ridque = p_umridque;
                ridans = p_umridans;
              } else {
                tclistprintf(emsgs, "Storing the password file was failed.");
              }
            }
          }
        }
        TCLIST *ulist = tcmpoollistnew(mpool);
        tcmapiterinit(g_users);
        const char *salt = NULL;
        const char *name;
        while((name = tcmapiternext2(g_users)) != NULL){
          const char *pass = tcmapiterval2(name);
          if(!strcmp(name, SALTNAME)){
            salt = pass;
          } else if(*name != SALTNAME[0]){
            TCMAP *user = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
            char *str = tcmpoolpushptr(mpool, tcstrdup(pass));
            char *pv = strchr(str, ':');
            if(pv){
              *(pv++) = '\0';
            } else {
              pv = "";
            }
            tcmapput2(user, "name", name);
            tcmapput2(user, "pass", str);
            tcmapput2(user, "info", pv);
            if(!strcmp(name, ADMINNAME)) tcmapput2(user, "admin", "true");
            tclistpushmap(ulist, user);
          }
        }
        tcmapprintf(vars, "titletip", "[user management]");
        tcmapput2(vars, "view", "users");
        if(tclistnum(ulist) > 0) tcmapputlist(vars, "userlist", ulist);
        if(salt) tcmapput2(vars, "salt", salt);
        tcmapput2(vars, "ridque", ridque);
        tcmapput2(vars, "ridans", ridans);
      }
  } else if (!strcmp(p_act, "files")) { // files view
	bool isdir;
	MY_DEBUG("g_upload=%s\n", g_upload);
	if (!g_upload || !tcstatfile(g_upload, &isdir, NULL, &mtime) || !isdir) {
		tclistprintf(emsgs, "The upload directory is missing.");
	} else if(!auth) {
		tclistprintf(emsgs, "The file management function is not available by outer users.");
	} else {
        if(post && (p_fmfilebuf || *p_fmpath != '\0')){
          if(seskey > 0 && p_seskey != seskey){
            tclistprintf(emsgs, "The session key is invalid (%u).", (unsigned int)p_seskey);
          } else if(!strcmp(p_fmmode, "new")){
            if(p_fmfilebuf && p_fmfilesiz > 0){
              const char *name = p_fmname;
              if(*name == '\0') name = p_fmfilename;
              if(*name == '\0') name = "_noname_";
              const char *ext = strrchr(name, '.');
              if(!ext && (ext = strrchr(p_fmfilename, '.')) != NULL)
                name = tcmpoolpushptr(mpool, tcsprintf("%s%s", name, ext));
              if(putfile(mpool, p_fmpath, name, p_fmfilebuf, p_fmfilesiz)){
                tcmapput2(vars, "newfile", name);
              } else {
                tclistprintf(emsgs, "Storing the file was failed.");
              }
            } else {
              tclistprintf(emsgs, "There is no data.");
            }
          } else if(!strcmp(p_fmmode, "repl")){
            if(p_fmfilebuf && p_fmfilesiz > 0){
              if(putfile(mpool, p_fmpath, "", p_fmfilebuf, p_fmfilesiz)){
                tcmapput2(vars, "replfile", p_fmname);
              } else {
                tclistprintf(emsgs, "Storing the file was failed.");
              }
            } else {
              tclistprintf(emsgs, "There is no data.");
            }
          } else if(!strcmp(p_fmmode, "del") && p_confirm){
            if(outfile(mpool, p_fmpath)){
              tcmapput2(vars, "delfile", p_fmname);
            } else {
              tclistprintf(emsgs, "Removing the file was failed.");
            }
          }
        } else {
          if(mtime <= p_ifmod){
            return HTTP_STATUS_304;
          }
          char numbuf[NUMBUFSIZ];
          tcdatestrhttp(mtime, 0, numbuf);
          tcmapput2(vars, "lastmod", numbuf);
        }
        int max = g_filenum;
        int skip = max * (p_page - 1);
        TCLIST *files = searchfiles(mpool, p_expr, p_order, max + 1, skip, p_fmthum);
        bool over = false;
        if(tclistnum(files) > max){
          tcfree(tclistpop2(files));
          over = true;
        }
        tcmapprintf(vars, "titletip", "[file management]");
        tcmapput2(vars, "view", "files");
        if(p_page > 1) tcmapprintf(vars, "prev", "%d", p_page - 1);
        if(over) tcmapprintf(vars, "next", "%d", p_page + 1);
        if(tclistnum(files) > 0) tcmapputlist(vars, "files", files);
      }
  } else if (p_id > 0) { // single view
    if(!auth && !ridcookie){
      if(mtime <= p_ifmod){
        return HTTP_STATUS_304;
      }
      char numbuf[NUMBUFSIZ];
      tcdatestrhttp(mtime, 0, numbuf);
      tcmapput2(vars, "lastmod", numbuf);
    }
    TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, p_id));
    if(cols){
      setarthtml(mpool, cols, p_id, 0, false);
      if(checkfrozen(cols) && !admin){
        tcmapput2(cols, "frozen", "true");
      } else if(cancom){
        tcmapputkeep2(cols, "comnum", "0");
        tcmapputkeep2(cols, "cancom", "true");
      } else if(!strcmp(g_commentmode, "riddle")){
        tcmapputkeep2(cols, "comnum", "0");
        tcmapput2(vars, "ridque", ridque);
        tcmapput2(vars, "ridans", ridans);
      }
      const char *name = tcmapget2(cols, "name");
      if(name) tcmapput2(vars, "titletip", name);
      if(!strcmp(p_adjust, "front")){
        tcmapput2(vars, "view", "front");
      } else {
        tcmapput2(vars, "view", "single");
      }
      tcmapput2(vars, "robots", "index,follow");
      tcmapputmap(vars, "art", cols);
    } else {
      tcmapput2(vars, "view", "empty");
    }
    tcmapprintf(vars, "cond", "id:%lld", (long long)p_id);
  } else if (*p_name != '\0') { // single view or search view
    if(!auth){
      if(mtime <= p_ifmod){
        return HTTP_STATUS_304;
      }
      char numbuf[NUMBUFSIZ];
      tcdatestrhttp(mtime, 0, numbuf);
      tcmapput2(vars, "lastmod", numbuf);
    }
    int max = g_searchnum;
    int skip = max * (p_page - 1);
    const char *order = (*p_order == '\0') ? "_cdate" : p_order;
    TCLIST *res = searcharts(mpool, tdb, "name", p_name, order, max + 1, skip, false);
    int rnum = tclistnum(res);
    if(rnum < 1){
      tcmapput2(vars, "view", "empty");
      if(auth) tcmapput2(vars, "missname", p_name);
    } else if(rnum < 2 || p_confirm){
      int64_t id = tcatoi(tclistval2(res, 0));
      TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
      if(cols){
        setarthtml(mpool, cols, id, 0, false);
        if(checkfrozen(cols) && !admin){
          tcmapput2(cols, "frozen", "true");
        } else if(cancom){
          tcmapputkeep2(cols, "comnum", "0");
          tcmapputkeep2(cols, "cancom", "true");
        }
        const char *name = tcmapget2(cols, "name");
        if(name) tcmapput2(vars, "titletip", name);
        if(!strcmp(p_adjust, "front")){
          tcmapput2(vars, "view", "front");
        } else {
          tcmapput2(vars, "view", "single");
        }
        tcmapput2(vars, "robots", "index,follow");
        tcmapputmap(vars, "art", cols);
      } else {
        tcmapput2(vars, "view", "empty");
      }
    } else {
      tcmapprintf(vars, "hitnum", "%d", rnum);
      TCLIST *arts = tcmpoollistnew(mpool);
      for(int i = 0; i < rnum && i < max; i++){
        int64_t id = tcatoi(tclistval2(res, i));
        TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
        if(cols){
          setarthtml(mpool, cols, id, 1, true);
          tclistpushmap(arts, cols);
        }
      }
      if(tclistnum(arts) > 0){
        tcmapprintf(vars, "titletip", "[name:%s]", p_name);
        tcmapput2(vars, "view", "search");
        tcmapput2(vars, "robots", "noindex,follow");
        if(p_page > 1) tcmapprintf(vars, "prev", "%d", p_page - 1);
        if(rnum > max) tcmapprintf(vars, "next", "%d", p_page + 1);
        tcmapputlist(vars, "arts", arts);
      } else {
        tcmapput2(vars, "view", "empty");
      }
    }
    tcmapprintf(vars, "cond", "name:%s", p_name);
  } else if (!strcmp(p_act, "search")) { // search view
    if(!auth){
      if(mtime <= p_ifmod){
        return HTTP_STATUS_304;
      }
      char numbuf[NUMBUFSIZ];
      tcdatestrhttp(mtime, 0, numbuf);
      tcmapput2(vars, "lastmod", numbuf);
    }
    tcmapprintf(vars, "titletip", "[search]");
    tcmapput2(vars, "view", "search");
    tcmapput2(vars, "robots", "noindex,follow");
    int max = g_searchnum;
    int skip = max * (p_page - 1);
    TCLIST *res = searcharts(mpool, tdb, p_cond, p_expr, p_order, max + 1, skip, true);
    int rnum = tclistnum(res);
    TCLIST *arts = tcmpoollistnew(mpool);
    for(int i = 0; i < rnum && i < max; i++){
      int64_t id = tcatoi(tclistval2(res, i));
      TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
      if(cols){
        setarthtml(mpool, cols, id, 1, true);
        tclistpushmap(arts, cols);
      }
    }
    if(tclistnum(arts) > 0){
      if(*p_expr != '\0') tcmapprintf(vars, "titletip", "[search:%s]", p_expr);
      if(p_page > 1) tcmapprintf(vars, "prev", "%d", p_page - 1);
      if(rnum > max) tcmapprintf(vars, "next", "%d", p_page + 1);
      if(tcmapget2(vars, "prev") || tcmapget2(vars, "next")) tcmapput2(vars, "page", "true");
      tcmapputlist(vars, "arts", arts);
    }
    if(*p_cond != '\0'){
      tcmapprintf(vars, "hitnum", "%d", rnum);
    } else {
      res = tcmpoollistnew(mpool);
      char numbuf[NUMBUFSIZ];
      tcdatestrwww(now, INT_MAX, numbuf);
      int year = tcatoi(numbuf);
      int minyear = year;
      res = searcharts(mpool, tdb, "cdate", "x", "_cdate", 1, 0, true);
      if(tclistnum(res) > 0){
        int64_t id = tcatoi(tclistval2(res, 0));
        TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
        const char *value = cols ? tcmapget2(cols, "cdate") : NULL;
        if(value){
          int64_t cdate = tcstrmktime(value);
          tcdatestrwww(cdate, INT_MAX, numbuf);
          minyear = tcatoi(numbuf);
        }
      }
      TCLIST *arcyears = tcmpoollistnew(mpool);
      for(int i = 0; i < 100 && year >= minyear; i++){
        sprintf(numbuf, "%04d", year);
        res = searcharts(mpool, tdb, "cdate", numbuf, "cdate", 1, 0, true);
        if(tclistnum(res) > 0){
          TCMAP *arcmonths = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
          for(int month = 0; month <= 12; month++){
            sprintf(numbuf, "%04d-%02d", year, month);
            res = searcharts(mpool, tdb, "cdate", numbuf, "cdate", 1, 0, true);
            rnum = tclistnum(res);
            sprintf(numbuf, "%02d", month);
            if(rnum > 0) tcmapprintf(arcmonths, numbuf, "%d", rnum);
          }
          if(tcmaprnum(arcmonths) > 0){
            tcmapprintf(arcmonths, "year", "%d", year);
            tclistpushmap(arcyears, arcmonths);
          }
        }
        year--;
      }
      if(tclistnum(arcyears) > 0) tcmapputlist(vars, "arcyears", arcyears);
    }
  } else if (*g_frontpage != '\0' && strcmp(p_act, "timeline")) { // front view
    if(!auth){
      if(mtime <= p_ifmod){
        return HTTP_STATUS_304;
      }
      char numbuf[NUMBUFSIZ];
      tcdatestrhttp(mtime, 0, numbuf);
      tcmapput2(vars, "lastmod", numbuf);
    }
    int64_t id = 0;
    const char *name = "";
    if(tcstrfwm(g_frontpage, "id:")){
      id = tcatoi(strchr(g_frontpage, ':') + 1);
    } else if(tcstrfwm(g_frontpage, "name:")){
      name = strchr(g_frontpage, ':') + 1;
    } else {
      name = g_frontpage;
    }
    if(id < 1 && *name != '\0'){
      TCLIST *res = searcharts(mpool, tdb, "name", name, "_cdate", 1, 0, false);
      if(tclistnum(res) > 0) id = tcatoi(tclistval2(res, 0));
    }
    tcmapput2(vars, "view", "front");
    tcmapput2(vars, "robots", "index,follow");
    if(id > 0){
      TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, id));
      if(cols){
        setarthtml(mpool, cols, id, 0, false);
        if(checkfrozen(cols) && !admin) tcmapput2(cols, "frozen", "true");
        tcmapputmap(vars, "art", cols);
      }
    }
  } else { // timeline view
    if(!auth){
      if(mtime <= p_ifmod){
        return HTTP_STATUS_304;
      }
      char numbuf[NUMBUFSIZ];
      tcdatestrhttp(mtime, 0, numbuf);
      tcmapput2(vars, "lastmod", numbuf);
    }
    int max = !strcmp(p_format, "atom") ? g_feedlistnum : g_listnum;
    int skip = max * (p_page - 1);
    TCLIST *res = searcharts(mpool, tdb, NULL, NULL, p_order, max + 1, skip, true);
    int rnum = tclistnum(res);
    if(rnum < 1){
      tcmapput2(vars, "view", "empty");
    } else {
      TCLIST *arts = tcmpoollistnew(mpool);
      for(int i = 0; i < rnum && i < max ; i++){
        int64_t id = tcatoi(tclistval2(res, i));
        TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
        if(cols){
          setarthtml(mpool, cols, id, 1, false);
          tclistpushmap(arts, cols);
        }
      }
      if(tclistnum(arts) > 0){
        if(*p_act != '\0'){
          if(!strcmp(p_order, "_cdate")){
            tcmapput2(vars, "titletip", "[old creation]");
          } else if(!strcmp(p_order, "mdate")){
            tcmapput2(vars, "titletip", "[recent modification]");
          } else if(!strcmp(p_order, "_mdate")){
            tcmapput2(vars, "titletip", "[old modification]");
          } else if(!strcmp(p_order, "xdate")){
            tcmapput2(vars, "titletip", "[recent comment]");
          } else if(!strcmp(p_order, "_xdate")){
            tcmapput2(vars, "titletip", "[old comment]");
          } else {
            tcmapput2(vars, "titletip", "[newcome articles]");
          }
        }
        tcmapput2(vars, "view", "timeline");
        tcmapput2(vars, "robots", "index,follow");
        if(p_page > 1) tcmapprintf(vars, "prev", "%d", p_page - 1);
        if(rnum > max) tcmapprintf(vars, "next", "%d", p_page + 1);
        if(tcmapget2(vars, "prev") || tcmapget2(vars, "next")) tcmapput2(vars, "page", "true");
        tcmapputlist(vars, "arts", arts);
      } else {
        tcmapput2(vars, "view", "empty");
      }
    }
  }
// side bar -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
  if (g_sidebarnum > 0 && strcmp(p_format, "atom")) {
    TCLIST *res = searcharts(mpool, tdb, NULL, NULL, "cdate", g_sidebarnum, 0, true);
    int rnum = tclistnum(res);
    TCLIST *arts = tcmpoollistnew(mpool);
    for(int i = 0; i < rnum; i++){
      int64_t id = tcatoi(tclistval2(res, i));
      TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
      if(cols){
        setarthtml(mpool, cols, id, 1, true);
        tclistpushmap(arts, cols);
      }
    }
    if(tclistnum(arts) > 0) tcmapputlist(vars, "sidearts", arts);
    res = searcharts(mpool, tdb, NULL, NULL, "xdate", g_sidebarnum, 0, true);
    rnum = tclistnum(res);
    TCLIST *coms = tcmpoollistnew(mpool);
    for(int i = 0; i < rnum; i++){
      int64_t id = tcatoi(tclistval2(res, i));
      TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
      if(cols){
        rp = tcmapget2(cols, "comments");
        if(rp && *rp != '\0'){
          TCLIST *lines = tcmpoolpushlist(mpool, tcstrsplit(rp, "\n"));
          int left = g_sidebarnum;
          for(int i = tclistnum(lines) - 1; i >= 0 && left > 0; i--, left--){
            rp = tclistval2(lines, i);
            char *co = strchr(rp, '|');
            if(co){
              *(co++) = '\0';
              char *ct = strchr(co, '|');
              if(ct){
                *(ct++) = '\0';
                COMMENT com;
                memset(&com, 0, sizeof(com));
                com.id = id;
                com.date = tcatoi(rp);
                com.owner = co;
                com.text = ct;
                tclistpush(coms, &com, sizeof(com));
              }
            }
          }
        }
      }
    }
    int cnum = tclistnum(coms);
    if(cnum > 0){
      tclistsortex(coms, comparecomments);
      TCLIST *comments = tcmpoolpushlist(mpool, tclistnew2(cnum));
      for(int i = 0; i < cnum && i < g_sidebarnum; i++){
        COMMENT *com = (COMMENT *)tclistval2(coms, i);
        TCMAP *comment = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
        tcmapprintf(comment, "id", "%lld", (long long)com->id);
        char numbuf[NUMBUFSIZ];
        tcdatestrwww(com->date, INT_MAX, numbuf);
        tcmapput2(comment, "date", numbuf);
        tcmapput2(comment, "datesimple", datestrsimple(numbuf));
        tcmapput2(comment, "owner", com->owner);
        tcmapput2(comment, "text", com->text);
        TCXSTR *xstr = tcmpoolxstrnew(mpool);
        wikitohtmlinline(xstr, com->text, g_scriptname, g_uploadpub);
        tcmapput(comment, "texthtml", 8, tcxstrptr(xstr), tcxstrsize(xstr));
        tclistpushmap(comments, comment);
      }
      tcmapputlist(vars, "sidecoms", comments);
    }
    tcmapput2(vars, "sidebar", "true");
  }
// close the database -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
  if (!tctdbclose(tdb))
	setdberrmsg(emsgs, tdb, "Closing the database was failed.");
// generete the output -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
  if(tclistnum(emsgs) > 0) tcmapputlist(vars, "emsgs", emsgs);
  if(tcxstrsize(comquery) > 0) tcmapput2(vars, "comquery", tcxstrptr(comquery));
  if(!strcmp(p_act, "logout")) tcmapout2(vars, "lastmod");
  if(auth && p_user != '\0'){
    tcmapput2(vars, "username", p_user);
    if(userinfo && *userinfo != '\0') tcmapput2(vars, "userinfo", userinfo);
    if(seskey > 0) tcmapprintf(vars, "seskey", "%llu", (unsigned long long)seskey);
  }
  if(auth) tcmapput2(vars, "auth", "true");
  if(admin) tcmapput2(vars, "admin", "true");
  if(cancom) tcmapput2(vars, "cancom", "true");
  char numbuf[NUMBUFSIZ];
  tcdatestrwww(now, INT_MAX, numbuf);
  tcmapput2(vars, "now", numbuf);
  tcdatestrwww(mtime, INT_MAX, numbuf);
  tcmapput2(vars, "mtime", numbuf);
  tcmapput2(vars, "mtimesimple", datestrsimple(numbuf));
  if(tcmapget2(vars, "prev") || tcmapget2(vars, "next")) tcmapput2(vars, "page", "true");
  tcmapputkeep2(vars, "view", "error");
  tcmapput2(vars, "format", !strcmp(p_format, "atom") ? "atom" : "html");

	state->out_type = (!strcmp(p_format, "atom")) ? "application/atom+xml" :
		(!strcmp(p_format, "xhtml")) ? "application/xhtml+xml" :
		(!strcmp(p_format, "xml")) ? "application/xml" : "text/html; charset=UTF-8";
	tcmapput2(vars, "mimetype", state->out_type);

	char *tmplstr = trimxmlchars(tcmpoolpushptr(mpool, tctmpldump(g_tmpl, vars)));
	while (*tmplstr > '\0' && *tmplstr <= ' ') {
		tmplstr++;
	}
	state->out_buf = tmplstr;
	state->out_len = strlen(tmplstr);

	if (authcookie) 
		vars_add(arg, "Set-Cookie", tcmpoolpushptr(mpool,
			tcsprintf("auth=%s; ",authcookie)));
	if (ridcookie)
		vars_add(arg, "Set-Cookie", tcmpoolpushptr(mpool,
			tcsprintf("riddle=%s; ",ridcookie)));
	if (*p_comowner != '\0' && checkusername(p_comowner))
		vars_add(arg, "Set-Cookie", tcmpoolpushptr(mpool,
			tcsprintf("comowner=%s; ",p_comowner)));

	return HTTP_STATUS_200;
}

/* plurc or gtfo -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */

#define PLURK_KEY "api_key=d6mOj6QFh4cFSFevRx2ZAvMZoYXW89TK"
#define PLURK_HOST "www.plurk.com"
#define PLURK_PORT "80"

#define BUFRECVSIZE	4096	/* org: 1024 */
#define REQUEST_SIZE	4096	/* org: 1024 */
#define SESSION_SIZE	1024	/* org: 512  */
#define RESPONSE_SIZE	40960

static char reply[RESPONSE_SIZE];
static char* ssl_cert = NULL;	/* OpenSSL cert */
static char* session = NULL;	/* [SESSION_SIZE]; set, if session has been initialized */
static SSL_CTX * ctx = NULL;
static BIO * bio = NULL;

static const char* PLURK_LOGIN =	"GET /API/Users/login?%s&%s HTTP/1.0\r\nHost: %s\r\nCookie: %s\r\n\r\n";
static const char* PLURK_LOGOUT =	"GET /API/Users/logout?%s&%s HTTP/1.0\r\nHost: %s\r\nCookie: %s\r\n\r\n";
static const char* PLURK_ADD =		"GET /API/Timeline/plurkAdd?%s&%s HTTP/1.0\r\nHost: %s\r\nCookie: %s\r\n\r\n";
static const char* PLURK_RESPS_GET =	"GET /API/Responses/get?%s&%s HTTP/1.0\r\nHost: %s\r\nCookie: %s\r\n\r\n";
static const char* PLURK_RESPS_ADD =	"GET /API/Responses/responseAdd?%s&%s HTTP/1.0\r\nHost: %s\r\nCookie: %s\r\n\r\n";
static const char* PLURK_OPROF_GET =	"GET /API/Profile/getOwnProfile?%s&%s HTTP/1.0\r\nHost: %s\r\nCookie: %s\r\n\r\n";
static const char* PLURK_PPROF_GET =	"GET /API/Profile/getPublicProfile?%s&%s HTTP/1.0\r\nHost: %s\r\nCookie: %s\r\n\r\n";

/* this one represents HTTP Response - splitted for headers and body */
struct plurk_response
{
	char *headers;
	char *body;
};

char *urlencode(char *content)
{
	/* simple urlencode implementation TODO: make it better */
	char *ptr = content;
	char *encoded = malloc(421);
	char *penc = encoded;

	while (*ptr) {
		if (*ptr==' ')
			*penc++ = '+';
		else if  (*ptr=='/') {
			memcpy(penc, "%2f", 3);
			penc+=3;
		}
		else if (*ptr=='#') {
			memcpy(penc, "%23", 3);
			penc+=3;
		}
		else
			*penc++ = *ptr;		
		ptr++;
	};
	*penc = '\0';
	
	return encoded;
};


static void json_dump( json_object* obj, const char* name, int tab )
{
	int i;
	char buf[64];
	const char *ptr;

	if( obj ) {
	switch( json_object_get_type(obj) ) {
		case json_type_array :
			for( i = 0; i < json_object_array_length(obj); i++ ) {
				sprintf( buf, "%s[%d]", name, i );
				json_dump( json_object_array_get_idx(obj, i), buf, tab + 4 );
			
			}
			return;
		case json_type_object :
			MY_DEBUG( "%*s%s:\n", tab, " ", name );
			{
				json_object_object_foreach(obj, key, val) {
					json_dump( val, key, tab + 4 );
  				}
			}
			return;
		default :
			break;
	}
	}
	ptr = (obj==NULL) ? "NULL" : json_object_to_string(obj);
	MY_DEBUG( "%*s%s: %s.\n", tab, " ", name, ptr );
	return;
}

/*
 * This function will be called on SSI directive <!--#if true -->, or
 * <!--#elif true -->, and 'returns' TRUE condition
 */
static void ssi_test_true(struct shttpd_arg *arg)
{
	arg->flags |= SHTTPD_SSI_EVAL_TRUE;
}

/*
 * This function will be called on SSI directive <!--#if false -->, or
 * <!--#elif false -->, and 'returns' FALSE condition
 */
static void ssi_test_false(struct shttpd_arg *arg)
{
	/* Do not set SHTTPD_SSI_EVAL_TRUE flag, that means FALSE */
}

/*
 * This function will be called on SSI <!--#call shttpd_version -->
 */
static void ssi_shttpd_version(struct shttpd_arg *arg)
{
	shttpd_printf(arg, "%s", shttpd_version());
	arg->flags |= SHTTPD_END_OF_OUTPUT;
}

static void ssi_http_header(struct shttpd_arg *arg)
{
	const char* header = NULL;

	if (arg->in.buf != NULL)
		header = shttpd_get_header(arg, arg->in.buf);
	shttpd_printf(arg, "%s", header ? header : "(NULL)");
	arg->flags |= SHTTPD_END_OF_OUTPUT;
}

/*
 * This callback function is attached to the "/" and "/abc.html" URIs,
 * thus is acting as "index.html" file. It shows a bunch of links
 * to other URIs, and allows to change the value of program's
 * internal variable. The pointer to that variable is passed to the
 * callback function as arg->user_data.
 */
static void show_index(struct shttpd_arg *arg)
{
	int		*p = arg->user_data;	/* integer passed to us */
	char		value[20];
	const char	*host, *request_method, *query_string, *request_uri, *remote_user;
	time_t tm;

	request_method = shttpd_getenv(arg, "REQUEST_METHOD");
	request_uri = shttpd_getenv(arg, "REQUEST_URI");
	query_string = shttpd_getenv(arg, "QUERY_STRING");
	remote_user = shttpd_getenv(arg, "REMOTE_USER");

	/* Change the value of integer variable */
	value[0] = '\0';
	if (!strcmp(request_method, "POST")) {
		/* If not all data is POSTed, wait for the rest */
		if (arg->flags & SHTTPD_MORE_POST_DATA)
			return;
		(void) shttpd_get_var("name1", arg->in.buf, arg->in.len,
		    value, sizeof(value));
	} else if (query_string != NULL) {
		(void) shttpd_get_var("name1", query_string,
		    strlen(query_string), value, sizeof(value));
	}
	if (value[0] != '\0') {
		*p = atoi(value);

		/*
		 * Suggested by Luke Dunstan. When POST is used,
		 * send 303 code to force the browser to re-request the
		 * page using GET method. This prevents the possibility of
		 * the user accidentally resubmitting the form when using
		 * Refresh or Back commands in the browser.
		 */
		if (!strcmp(request_method, "POST")) {
			shttpd_printf(arg, "HTTP/1.1 303 See Other\r\n"
				"Location: %s\r\n\r\n", request_uri);
			arg->flags |= SHTTPD_END_OF_OUTPUT;
			return;
		}
	}

	shttpd_printf(arg, "%s",
		"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
		"<html><body><h1>Welcome to embedded example of SHTTPD");
	shttpd_printf(arg, " v. %s </h1><ul>", shttpd_version());

	shttpd_printf(arg, "<li><code>REQUEST_METHOD: %s "
	    "REQUEST_URI: \"%s\" QUERY_STRING: \"%s\""
	    " REMOTE_ADDR: \"%s\" REMOTE_USER: \"%s\"</code><hr>",
	    request_method, request_uri,
	    query_string ? query_string : "(null)",
	    shttpd_getenv(arg, "REMOTE_ADDR"),
	    remote_user ? remote_user : "(null)");
	shttpd_printf(arg, "<li>Internal int variable value: <b>%d</b>", *p);

	shttpd_printf(arg, "%s",
		"<form method=\"GET\">Enter new value: "
		"<input type=\"text\" name=\"name1\"/>"
		"<input type=\"submit\" "
		"value=\"set new value using GET method\"></form>");
	shttpd_printf(arg, "%s",
		"<form method=\"POST\">Enter new value: "
		"<input type=\"text\" name=\"name1\"/>"
		"<input type=\"submit\" "
		"value=\"set new value using POST method\"></form>");
		
	shttpd_printf(arg, "%s",
		"<hr>"
		"<li><a href=\"/secret\">Protected page</a> (guest:guest)<hr>"
		"<li><a href=\"/huge\">Output lots of data</a><hr>"
		"<li><a href=\"/my_cache/\">Aliased ALIAS_DIR directory</a><hr>");
	shttpd_printf(arg, "%s",
		"<li><a href=\"/Makefile\">Regular file (Makefile)</a><hr>"
		"<li><a href=\"/ssi_test.shtml\">SSI file "
			"(ssi_test.shtml)</a><hr>"
		"<li><a href=\"/users/joe/\">Wildcard URI example</a><hr>"
		"<li><a href=\"/not-existent/\">Custom 404 handler</a><hr>"
		"<li><a href=\"/config\">Configure parameters</a><hr>");

	host = shttpd_getenv(arg, "HTTP_HOST");
	shttpd_printf(arg, "<li>'Host' header value: [%s]<hr>",
	    host ? host : "NOT SET");

	time(&tm);
	shttpd_printf(arg,
	    "<li>Current time: %s<hr>", ctime(&tm));

	shttpd_printf(arg, "<li>Upload file example. "
	    "<form method=\"post\" enctype=\"multipart/form-data\" "
	    "action=\"/post\"><input type=\"file\" name=\"file\">"
	    "<input type=\"submit\"></form>");

	shttpd_printf(arg, "%s", "</body></html>");
	arg->flags |= SHTTPD_END_OF_OUTPUT;
}

/*
 * This callback is attached to the URI "/post"
 * It uploads file from a client to the server. This is the demostration
 * of how to use POST method to send lots of data from the client.
 * The uploaded file is saved into "uploaded.txt".
 * This function is called many times during single request. To keep the
 * state (how many bytes we have received, opened file etc), we allocate
 * a "struct state" structure for every new connection.
 */
static void show_post(struct shttpd_arg *arg)
{
	const char	*s, *path = "uploaded.txt";
	struct state {
		size_t	cl;		/* Content-Length	*/
		size_t	nread;		/* Number of bytes read	*/
		FILE	*fp;
	} *state;

	/* If the connection was broken prematurely, cleanup */
	if (arg->flags & SHTTPD_CONNECTION_ERROR && arg->state) {
		(void) fclose(((struct state *) arg->state)->fp);
		free(arg->state);
	} else if ((s = shttpd_getenv(arg, "HTTP_CONTENT-LENGTH")) == NULL) {
		shttpd_printf(arg, "HTTP/1.0 411 Length Required\n\n");
		arg->flags |= SHTTPD_END_OF_OUTPUT;
	} else if (arg->state == NULL) {
		/* New request. Allocate a state structure, and open a file */
		arg->state = state = calloc(1, sizeof(*state));
		state->cl = strtoul(s, NULL, 10);
		state->fp = fopen(path, "wb+");
		shttpd_printf(arg, "HTTP/1.0 200 OK\n"
			"Content-Type: text/plain\n\n");
	} else {
		state = arg->state;

		/*
		 * Write the POST data to a file. We do not do any URL
		 * decoding here. File will contain form-urlencoded stuff.
		 */
		(void) fwrite(arg->in.buf, arg->in.len, 1, state->fp);
		state->nread += arg->in.len;

		/* Tell SHTTPD we have processed all data */
		arg->in.num_bytes = arg->in.len;

		/* Data stream finished? Close the file, and free the state */
		if (state->nread >= state->cl) {
			shttpd_printf(arg, "Written %d bytes to %s",
			    state->nread, path);
			(void) fclose(state->fp);
			free(state);
			arg->flags |= SHTTPD_END_OF_OUTPUT;
		}
	}
}

/*
 * This callback function is attached to the "/huge" URI.
 * It outputs binary data to the client.
 * The number of bytes already sent is stored directly in the arg->state.
 */
static void show_huge(struct shttpd_arg *arg)
{
	long int state = (long int)(arg->state);

	if (state == 0) {
		shttpd_printf(arg, "%s", "HTTP/1.1 200 OK\r\n");
		shttpd_printf(arg, "%s", "Content-Type: text/plain\r\n\r\n");
	}
	while (arg->out.num_bytes < arg->out.len) {
		arg->out.buf[arg->out.num_bytes] = state % 72 ? 'A' : '\n';
		arg->out.num_bytes++;
		state++;

		if (state > 1024 * 1024) {	/* Output 1Mb Kb of data */
			arg->flags |= SHTTPD_END_OF_OUTPUT;
			break;
		}
	}
	arg->state = (void *)state;
}

/* wkliang:20100715 fetchpage, plurc or gtfo -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */
static SSL_CTX * ssl_ctx = NULL;

typedef struct http_page { /* represents HTTP Response, splitted for headers and body */
	size_t headers_len;
	char *headers;
	size_t body_len;
	char *body;
	size_t page_len;
	char page[1];
} http_page;

#define HTTP_PAGE_SIZE	(65536-sizeof(struct http_page))
#define HTTP_BUFF_SIZE	1024

static char *getheader(const char *headers, char *header)
{
	unsigned int c, d;			/* counters */
	unsigned int isizes = strlen(headers);
	unsigned int isize = strlen(header);

	char* value = malloc(HTTP_BUFF_SIZE);
	memset(value, '\0', HTTP_BUFF_SIZE);
	for (c=0;c<isizes;c++) {
		if (tolower(headers[c])==tolower(header[0])) {	
			c++;
			for(d=1;(d<isize && c <= isizes);d++) {
			 /* checks if c is not bigger  */
				if (tolower(headers[c])!=tolower(header[d])) {
					break;
				};
				c++;			
			};
			if (d==isize) {	
				c += 2; 		/* skip ': ' */
				d = 0; 			/* reset counter */
				while((c<isizes) && (headers[c] != '\r')) {
					value[d] = headers[c];
					c++;
					d++;
				};
				return value;
			};
		};
	};
	return value;
}

static int fetchpage(const char* server, const int secure, const char* request, struct http_page *hpp)
{
	BIO* bio;
	ssize_t bytessent;
	unsigned int rsize = 0;
	unsigned int check_header = 1;
	char *ptr, *eoh = NULL;	/* end of headers */

	MY_DEBUG("server=%s, secure=%d, url=%d:[\n%s]\n",
		server, secure, strlen(request), request);
	if (secure) { /* over ssl */
		SSL *ssl = NULL;

		bio = BIO_new_ssl_connect(ssl_ctx);
		BIO_get_ssl(bio, &ssl);
		SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);

		BIO_set_conn_hostname(bio, server);
	}
	else { /* plain */
		char host[256];
		strcpy(host, server);
		bio = BIO_new_connect(host);
	};
	if ((bio == NULL) || (BIO_do_connect(bio) <= 0)) {
		ERR_print_errors_fp(stderr);
		return -1 * __LINE__;
	}
	bytessent = BIO_write(bio, request, strlen(request));

	hpp->page_len = 0;
	ptr = hpp->page;
	while ( (hpp->page_len < HTTP_PAGE_SIZE) &&
		((rsize = BIO_read(bio, ptr, HTTP_BUFF_SIZE)) > 0)) {
		ptr += rsize;
		hpp->page_len += rsize;
	};
	if ((eoh = strstr(hpp->page, "\r\n\r\n")) != NULL) {
		check_header = 0;
		*eoh = hpp->page[hpp->page_len] = '\0';
		hpp->headers = hpp->page;
		hpp->headers_len = eoh - hpp->headers;
		hpp->body = eoh + 4;
		hpp->body_len = hpp->page + hpp->page_len - hpp->body; 
	} 
	else {
		hpp->headers_len = hpp->body_len = 0;
		hpp->headers = hpp->body = hpp->page;
		hpp->page[0] = '\0';
	};
	BIO_free_all(bio);
	return atoi(hpp->page+9);	// skip HTTP/1.x nnn
}

static http_status json_fetchpage(struct shttpd_arg *arg)
{
	user_state *state = arg->state;
	json_object *my_obj = NULL;
	http_page *hpp = NULL;
	int rc;

	state->out_type = "text/javascript";
	state->out_buf = "{\"code\":\"-9999\"}";
	state->out_len = strlen(state->out_buf);

	my_obj = tcmpoolpush(state->mpool,json_object_new_object(),(void(*)(void*))jsobj_DecrRef);
	hpp = tcmpoolpush(state->mpool,calloc(1, sizeof(http_page)+HTTP_PAGE_SIZE),free);
	if (!my_obj || !hpp)
		return HTTP_STATUS_500;
	rc =  fetchpage(params_get(arg, "server", ""),
		atoi(params_get(arg, "secure", "0")),
		params_get(arg, "request", "GET / HTTP/1.0\r\n\r\n"), hpp);
	if (rc  < 0)
		return HTTP_STATUS_500;

	json_object_object_add(my_obj, "code", json_object_new_int(rc));
	json_object_object_add(my_obj, "cookie", json_object_new_string(
		getheader(hpp->headers, "set-cookie")));
	json_object_object_add(my_obj, "ctype", json_object_new_string(
			 getheader(hpp->headers, "content-type")));
	json_object_object_add(my_obj, "cbody", json_object_new_string(hpp->body));

	// wkliang:20130104 let tcmpool clean it
	// free(hpp);

	state->out_buf = json_object_to_string(my_obj);
	state->out_len = strlen(state->out_buf);
	return HTTP_STATUS_200;
}

/* wkliang:20110520 -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=- */

#define JSONMSG_NUM	1024

MUTEX JSONMSG_lock;
const char *JSONMSG_fname = "/home/wkliang/Desktop/chatlog.json";
json_object** JSONMSG_lst;
int JSONMSG_cnt, JSONMSG_idx;

static void json_save()
{
	FILE *fp;
	int i, idx;

	if ((fp = fopen(JSONMSG_fname, "w+")) != NULL) {
		for (i = 0, idx = JSONMSG_idx; i < JSONMSG_NUM; i++ ) {
			if (JSONMSG_lst[idx] != NULL) {
				fprintf(fp, "%s\n", json_object_to_string(JSONMSG_lst[idx]));
			}
			idx++;
			if (idx >= JSONMSG_NUM)
				idx = 0;
		}
		fclose(fp);
	}
	return;
}

time_t mktime_from_string(const char *s) // 13-01-31 17:54:27
{
	struct tm tmb;
	time_t tmt;

	tmb.tm_year = atoi(s) + 100;
	tmb.tm_mon = atoi(s+3) - 1;
	tmb.tm_mday = atoi(s+6);
	tmb.tm_hour = atoi(s+9);
	tmb.tm_min = atoi(s+12);
	tmb.tm_sec = atoi(s+15);

	tmt =  mktime(&tmb);
	// MY_DEBUG("%lu:%s.\n", tmt*1000, ctime(&tmt));
	return tmt;
}

static void json_restore()
{
	TCMPOOL *mpool = tcmpoolnew();
	TCTDB *tdb = tcmpoolpush(mpool,tctdbnew(),(void (*)(void*))tctdbdel);
	tctdbopen(tdb, "/home/wkliang/share/tokyopromenade/tctchat.tct", TDBOWRITER|TDBOCREAT);
	tctdbsetindex(tdb, "", TDBITDECIMAL | TDBITKEEP);
	char buf[BUFSIZ];
	FILE *fp;
	int i;

	if ((fp = fopen(JSONMSG_fname, "r")) != NULL) {
		for (i = 0; i < JSONMSG_NUM; i++ ) {
			if (NULL == fgets(buf, BUFSIZ, fp))
				break;
			// MY_DEBUG("%s(%d):%s\n", __FILE__, __LINE__, __func__);
			// MY_DEBUG("%s.\n", buf);
			struct json_object *jsobj = json_tokener_parse(buf);
			const char *jtime, *jname, *jtext;

			jtime = json_object_get_string(json_object_object_get(jsobj, "time"));
			jname = json_object_get_string(json_object_object_get(jsobj, "name"));
			jtext = json_object_get_string(json_object_object_get(jsobj, "text"));

			// MY_DEBUG("%lu,%s,%s,%s.\n", mktime_from_string(jtime) * 1000, jtime, jname, jtext); 

			char pkbuf[64];
			int pksiz = sprintf(pkbuf, "%lu",
				mktime_from_string(jtime) * 1000);
			TCMAP *cols = tcmpoolmapnew(mpool);
			tcmapput2(cols, "a", jname);
			tcmapput2(cols, "t", jtext);
    			tctdbtranbegin(tdb);
			if (!tctdbput(tdb, pkbuf, pksiz, cols))
				MY_DEBUG("The message is ignored.\n");
			tctdbtrancommit(tdb);

			if (JSONMSG_lst[JSONMSG_idx]) { // discard old message
				jsobj_DecrRef(JSONMSG_lst[JSONMSG_idx]);
			}
			JSONMSG_lst[JSONMSG_idx] = jsobj;
			JSONMSG_idx++;
			if (JSONMSG_idx >= JSONMSG_NUM)
				JSONMSG_idx = 0;
		}
		fclose(fp);
	}
	if (mpool) {
		tcmpooldel(mpool);
	}
	return;
}

static http_status quickstart(struct shttpd_arg *arg)
{
	user_state *state = arg->state;
	int xval, i, m;
	
	xval = atoi(params_get(arg, "xval", "0"));

	printbuf_reset(state->pbuf);
	sprintbuf(state->pbuf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n");
	sprintbuf(state->pbuf, "<response>\r\n");
	if (xval < 2)
		sprintbuf(state->pbuf, "%d is not a valid number!", xval); // should URL encoding?
	else {
		m = (int)sqrt((double)xval);
		for (i=2; i<=m; i++)
			if (xval % i == 0)
				break;
		if (i>m)
			sprintbuf(state->pbuf, "%d is prime number!", xval);
		else
			sprintbuf(state->pbuf, "%d can be divied by %d!", xval, i);
	}
	sprintbuf(state->pbuf, "</response>\r\n");

	state->out_type = "text/xml";
	state->out_buf = state->pbuf->buf;
	state->out_len = strlen(state->out_buf);
	return HTTP_STATUS_200;
}

static http_status chat_xml(struct shttpd_arg *arg)
{
	user_state *state = arg->state;
	static int chat_id = 0;
	int clearChat = 0;
	char tm_s[64];
	time_t tm = time(NULL);

	strftime(tm_s, sizeof(tm_s), "%H:%M:%S", localtime(&tm));
	if ( ++chat_id >= 100 ) {
		chat_id = 0;
		clearChat = 1;
	}
	printbuf_reset(state->pbuf);
	sprintbuf(state->pbuf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n");
	sprintbuf(state->pbuf, "<response>\r\n"
		"<clear>%s</clear>" "<messages>" "<message>\r\n"
		"<id>%d</id><color>#000000</color>\r\n"
		"<time>%s-%02d</time>\r\n"
		"<name>%s</name>\r\n"
		"<text>%s,%s,%s.</text></message>" "</messages>" "</response>\r\n",
		clearChat ? "true" : "false", chat_id, tm_s, chat_id,
			params_get(arg, "name", "nobody"),
			params_get(arg, "mode", "unknown"),
			params_get(arg, "id", "NaN"),
			params_get(arg, "message", "nothing"));

	state->out_type = "text/xml";
	state->out_buf = state->pbuf->buf;
	state->out_len = strlen(state->out_buf);
	return HTTP_STATUS_200;
}

static http_status chat_getcolor(struct shttpd_arg *arg)
{
	user_state *state = arg->state;
	int val_x, val_y;

	val_x = atoi(params_get(arg, "offsetx", "0"));
	val_y = atoi(params_get(arg, "offsety", "0"));

	printbuf_reset(state->pbuf);
	sprintbuf( state->pbuf, "#%02x%02x%02x",
		(val_x&0x0F) | ((val_y&0x0F)<<4),
		((val_x>>4)&0x0F) | (((val_y>>4)&0x0F)<<4),
		((val_x>>8)&0x0F) | (((val_y>>8)&0x0F)<<4) );
	
	state->out_type = "text/plain";
	state->out_buf = state->pbuf->buf;
	state->out_len = strlen(state->out_buf);
	return HTTP_STATUS_200;
}

static int json_pollMessages(struct shttpd_arg *arg)
{
	user_state *state = arg->state;
	json_object *my_obj, *my_arr;
	int chat_idx, chat_cnt;

	chat_idx = atoi(params_get(arg, "id", "-1"));
	if (chat_idx < 0) {
		chat_idx = JSONMSG_idx - 64;
		if (chat_idx < 0)
			chat_idx += JSONMSG_NUM;
	} else if (chat_idx >= JSONMSG_NUM) {
		chat_idx = JSONMSG_idx;
		chat_idx++;
		 if (chat_idx >= JSONMSG_NUM)
			chat_idx = 0;
	}
	if ((time(NULL) - state->start_tm < 60) &&
	    (chat_idx == JSONMSG_idx)) {
		// iaxc_millisleep(100);
		return HTTP_STATUS_WAITING;
	}
	my_obj = tcmpoolpush(state->mpool, json_object_new_object(), (void(*)(void*))jsobj_DecrRef);
	my_arr = json_object_new_array();
	if (!my_obj || !my_arr) {
		state->out_type = "text/javascript";
		state->out_buf = "{\"status\":\"error!\"}";
		state->out_len = strlen(state->out_buf);
		return HTTP_STATUS_500;
	}
	MY_DEBUG("%s(%d)\n", __func__, chat_idx);
	MUTEXLOCK(&JSONMSG_lock);
	for (chat_cnt = 0; chat_cnt < 64; ) {
		if (chat_idx == JSONMSG_idx)
			break;
		if (JSONMSG_lst[chat_idx] != NULL) {
			jsobj_IncrRef(JSONMSG_lst[chat_idx]);
			json_object_array_add(my_arr, JSONMSG_lst[chat_idx]);
			chat_cnt++;
		}
		chat_idx++;
		if (chat_idx >= JSONMSG_NUM)
			chat_idx = 0;
	}
	json_object_object_add(my_obj, "messages", my_arr);
	json_object_object_add(my_obj, "lastMessageID",
		json_object_new_int(chat_idx));
	json_object_object_add(my_obj, "clearChat",
		json_object_new_boolean(chat_cnt>100));
	MUTEXUNLOCK(&JSONMSG_lock);
	MY_DEBUG("%s(%d)\n", __func__, chat_idx);

	// jsobj_dump(my_obj, "", 0);
	state->out_type = "text/javascript";
	state->out_buf = json_object_to_string(my_obj);
	state->out_len = strlen(state->out_buf);
	MY_DEBUG("JSONMSG_idx=%d, chat_cnt=%d, %s.\n", JSONMSG_idx, chat_cnt, state->out_buf);
	
	return HTTP_STATUS_200;
}

static int json_chat(struct shttpd_arg *arg)
{
	user_state *state = arg->state;

	if (!strcasecmp(params_get(arg,"method","unknown"),"SendAndRetrieveNew")) {
		char buf[BUFSIZ];
		MUTEXLOCK(&JSONMSG_lock);
		if (JSONMSG_lst[JSONMSG_idx] != NULL)
			jsobj_DecrRef(JSONMSG_lst[JSONMSG_idx]);
		JSONMSG_lst[JSONMSG_idx] = json_object_new_object();
		strftime(buf, sizeof(buf), "%y-%m-%d %H:%M:%S", localtime(&state->start_tm));
		json_object_object_add(JSONMSG_lst[JSONMSG_idx], "time",
			json_object_new_string(buf));
		json_object_object_add(JSONMSG_lst[JSONMSG_idx], "color",
			json_object_new_string(params_get(arg, "color", "black")));
		json_object_object_add(JSONMSG_lst[JSONMSG_idx], "name",
			json_object_new_string(params_get(arg, "name", "nobody")));
		json_object_object_add(JSONMSG_lst[JSONMSG_idx], "text",
			json_object_new_string(params_get(arg, "text", "nothing")));
		JSONMSG_idx++;
		if (JSONMSG_idx >= JSONMSG_NUM)
			JSONMSG_idx = 0;
		json_save();
		MUTEXUNLOCK(&JSONMSG_lock);
		state->out_type = "text/javascript";
		state->out_buf = "{\"status\":\"OK!\"}";
		state->out_len = strlen(state->out_buf);
		return HTTP_STATUS_200;
	}
	arg->user_data = json_pollMessages;
	return json_pollMessages(arg);
}

static int tctchat(struct shttpd_arg *arg)
{
	user_state *state = arg->state;
	TCMPOOL *mpool = state->mpool;
	char fname[BUFSIZ];

	TCTMPL *tmpl = tcmpoolpush(mpool, tctmplnew(), (void (*)(void *))tctmpldel);
	sprintf(fname, "%s/%s", g_docroot, "tctchat.tmpl");
	tctmplload2(tmpl, fname);

  const char *dbpath = tctmplconf(tmpl, "dbpath");
  if(!dbpath) dbpath = "tctchat.tct";
  TCLIST *msgs = tcmpoollistnew(mpool);
  TCTDB *tdb = tcmpoolpush(mpool, tctdbnew(), (void (*)(void *))tctdbdel);

	const char *type = params_get(arg, "type", "");
	const char *author = params_get(arg, "author", "");
	const char *text = params_get(arg, "text", "");
	const char *search = params_get(arg, "search", "");
	int page = tcatoi(params_get(arg, "page", "1"));
	MY_DEBUG("type=%s, author=%s, text=%s, search=%s, page=%d.\n",
		type, author, text, search, page);

  const char* rp = shttpd_getenv(arg, "REQUEST_METHOD");
  if(rp && !strcmp(rp, "POST") && *author != '\0' && *text != '\0' &&
     strlen(author) <= 32 && strlen(text) <= 1024){
    if(!tctdbopen(tdb, dbpath, TDBOWRITER | TDBOCREAT))
      tclistprintf(msgs, "The database could not be opened (%s).", tctdberrmsg(tctdbecode(tdb)));
    tctdbsetindex(tdb, "", TDBITDECIMAL | TDBITKEEP);
    char pkbuf[64];
    int pksiz = sprintf(pkbuf, "%.0f", tctime() * 1000);
    TCMAP *cols = tcmpoolmapnew(mpool);
    tcmapput2(cols, "a", author);
    tcmapput2(cols, "t", text);
    tctdbtranbegin(tdb);
    if(!tctdbputkeep(tdb, pkbuf, pksiz, cols)) tclistprintf(msgs, "The message is ignored.");
    tctdbtrancommit(tdb);
	MY_DEBUG("pk=%s, author=%s, text=%s.\n", pkbuf, author, text);
  } else {
    if(!tctdbopen(tdb, dbpath, TDBOREADER))
      tclistprintf(msgs, "The database could not be opened (%s).", tctdberrmsg(tctdbecode(tdb)));
  }
  TCLIST *logs = tcmpoollistnew(mpool);
  TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
  if(*search != '\0') tctdbqryaddcond(qry, "t", TDBQCFTSEX, search);
  tctdbqrysetorder(qry, "", TDBQONUMDESC);
  tctdbqrysetlimit(qry, 16, page > 0 ? (page - 1) * 16 : 0);
  TCLIST *res = tcmpoolpushlist(mpool, tctdbqrysearch(qry));
  int rnum = tclistnum(res);
  for(int i = rnum - 1; i >= 0; i--){
    int pksiz;
    const char *pkbuf = tclistval(res, i, &pksiz);
    TCMAP *cols = tcmpoolpushmap(mpool, tctdbget(tdb, pkbuf, pksiz));
    if(cols){
      tcmapprintf(cols, "pk", "%s", pkbuf);
      char date[64];
      tcdatestrwww(tcatoi(pkbuf) / 1000, INT_MAX, date);
      tcmapput2(cols, "d", date);
      const char *astr = tcmapget4(cols, "a", "");
      tcmapprintf(cols, "c", "c%02u", tcgetcrc(astr, strlen(astr)) % 12 + 1);
      tclistpushmap(logs, cols);
	MY_DEBUG("%s,%s,%s,%s.\n", pkbuf,date,astr,tcmapget4(cols,"t",""));
    }
  }
  TCMAP *vars = tcmpoolmapnew(mpool);
  if(tclistnum(msgs) > 0) tcmapputlist(vars, "msgs", msgs);
  if(tclistnum(logs) > 0) tcmapputlist(vars, "logs", logs);
  tcmapprintf(vars, "author", "%s", author);
  tcmapprintf(vars, "search", "%s", search);
  if(page > 1) tcmapprintf(vars, "prev", "%d", page - 1);
  if(rnum >= 16 && tctdbrnum(tdb) > page * 16) tcmapprintf(vars, "next", "%d", page + 1);

	// output
	state->out_type = !strcmp(type, "xml") ? "application/xml" :
		!strcmp(type, "xhtml") ? "application/xhtml+xml" : "text/html; charset=UTF-8";
	state->out_buf = tcmpoolpushptr(mpool, tctmpldump(tmpl, vars));
	state->out_len = strlen(state->out_buf);

	vars_add(arg, "Set-Cookie", tcmpoolpushptr(mpool,
			tcsprintf("author=%s; ", author)));
	return HTTP_STATUS_200;
}

// main entry for template and proc -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
static void user_proc(struct shttpd_arg *arg)
{
	user_state *state = arg->state;

	// MY_DEBUG("%s:%d:%s.\n", __FILE__, __LINE__, __func__);
	if (arg->flags & SHTTPD_CONNECTION_ERROR)
		goto end_proc;
	if (state == NULL) {	// 1st time entering
		if (!(state = arg->state = calloc(1, sizeof(*state))) ||
		    !(state->mpool = tcmpoolnew()) ||
		    !(state->pbuf = printbuf_new()) ||
		    !(state->params = tcmpoolpushmap(state->mpool,tcmapnew2(TINYBNUM))) ||
		    !(state->vars = tcmpoolpushmap(state->mpool,tcmapnew2(TINYBNUM)))) {
			showerror(arg, 500, "memory allocation failure.");
			goto end_proc;
		}
		if (g_users) {	// wkliang:20110606
			int64_t mtime;
			if (tcstatfile(g_password, NULL, NULL, &mtime) && mtime > g_starttime) {
				tcmapclear(g_users);
				readpasswd();
				g_starttime = mtime;
			}
		}
		state->start_tm = time(NULL);

		// wkliang:20110601 process already known headers
		const char *cp;
		if (!strcasecmp(shttpd_getenv(arg,"REQUEST_METHOD"), "POST")) {
			cp = shttpd_getenv(arg, "HTTP_CONTENT-LENGTH");
			if (cp && *cp) {
				state->in_len = tcatoi(cp);
				if (state->in_len > g_recvmax) {
					showerror(arg, 413, "The entity body was too long.");
					goto end_proc;
				}
			}
		}
		// MY_DEBUG("vars:%p, params:%p.\n", state->vars, state->params);
  		tcmapputmap(state->vars, "params", state->params);
		cp = shttpd_getenv(arg, "QUERY_STRING");
		if (cp && *cp)
			tcwwwformdecode(cp, state->params);
		cp = shttpd_getenv(arg, "HTTP_COOKIE");
		if (cp && *cp)
			tcwwwformdecode(cp, state->params);
		// MY_DEBUG("%s()\n", __func__);
	}
	if (time(NULL) - state->start_tm >= 90) {
		showerror(arg, 500, "timeout!");
		goto end_proc;
	} else {
		state->proc_cnt++;
#if 0
		MY_DEBUG("elapse=%ld, cnt=%d, cl=%d, in.len=%d, bpos=%d, chat_idx=%d,%d.\n",
			time(NULL)-state->start_tm, state->proc_cnt,
			state->in_len, arg->in.len, printbuf_bpos(state->pbuf),
 			JSONMSG_idx, state->chat_idx);
#endif
	}
	if (state->in_len > printbuf_bpos(state->pbuf)) {
		printbuf_memappend(state->pbuf, arg->in.buf, arg->in.len);
		arg->in.num_bytes = arg->in.len; // tell SHTTPD data processed
		if (state->in_len > printbuf_bpos(state->pbuf)) {
			arg->flags |= SHTTPD_MORE_POST_DATA;
			return ;	// wait for next run
		}
 		// post data recieved
 		MY_DEBUG("ct=%s, pbuf=%s.\n",
			shttpd_getenv(arg,"HTTP_CONTENT-TYPE"), printbuf_buf(state->pbuf));
		tcwwwformdecode2(printbuf_buf(state->pbuf), state->in_len,
			shttpd_getenv(arg, "HTTP_CONTENT-TYPE"), state->params);
		MY_DEBUG("%s()\n", __func__);
	}
	if (state->out_buf == NULL) { // process request
		http_status rc = ((user_proto)arg->user_data)(arg);
		if (rc == HTTP_STATUS_WAITING) {
			return ; // wkliang:20110602 comet, long poll
		}
		dump_tcmap("vars", state->vars);
		if (rc == HTTP_STATUS_304) {
			shttpd_printf(arg, "HTTP/1.1 304 Not Modified\r\n");
			shttpd_printf(arg, "Status: 304 Not Modified\r\n\r\n");
		} else if (rc != HTTP_STATUS_200) {
			showerror(arg, rc, "NG");
		} else {
			shttpd_printf(arg, "HTTP/1.1 200 OK\r\n");
			shttpd_printf(arg, "Pragma: no-cache\r\n");
			shttpd_printf(arg, "Cache-Control: no-cache, must-revalidate\r\n");
			{
			const char *cp = tcmapget4(state->vars, "lastmod", NULL);
			if (cp && *cp)
				 shttpd_printf(arg, "Last-Modified: %s\r\n", cp);
			cp = tcmapget4(state->vars, "Set-Cookie", NULL);
			if (cp && *cp && (g_sessionlife > 0))
				shttpd_printf(arg, "Set-Cookie: %s max-age=%d; path=/\r\n",
					cp, g_sessionlife);
			}
			shttpd_printf(arg, "X-Count: %lu\r\n", state->proc_cnt);
			shttpd_printf(arg, "X-Timer: %lu\r\n", time(NULL) - state->start_tm);
			shttpd_printf(arg, "Content-Type: %s\r\n",
				state->out_type ? state->out_type : "text/plain");
			shttpd_printf(arg, "Content-Length: %d\r\n\r\n", state->out_len);
		}
		state->out_cnt = 0;
	}
	if (state->out_len > state->out_cnt) {
		MY_DEBUG("%s:%d,%d\n", __func__, state->out_len, state->out_cnt);
		int siz = (arg->out.len-arg->out.num_bytes > state->out_len-state->out_cnt) ?
			state->out_len-state->out_cnt : arg->out.len-arg->out.num_bytes;
		memcpy(arg->out.buf+arg->out.num_bytes, state->out_buf+state->out_cnt, siz);
		arg->out.num_bytes += siz;
		state->out_cnt += siz;
		if (state->out_len >= state->out_cnt) {
			MY_DEBUG("%s:%d,%d\n", __func__, state->out_len, state->out_cnt);
			return;
		}
	}
end_proc:
	if (state != NULL) {
		if (state->pbuf)
			printbuf_free(state->pbuf);
		if (state->mpool)
			tcmpooldel(state->mpool);
		free(state);
	}
	arg->flags |= SHTTPD_END_OF_OUTPUT;
	return;
}

void user_done(struct shttpd_ctx* ctx)
{
	tcmpooldel(g_mpool);
	MUTEXDESTROY(&JSONMSG_lock);
}

void user_init(struct shttpd_ctx* ctx)
{
	ssl_ctx = ctx->ssl_ctx;
//	ssl_cert = ctx_options[OPT_SSL_CERTIFICATE];	// "/home/wkliang/servent/shttpd.pem";

	shttpd_set_option(ctx, "cfg_uri", "/config");

	/* Register an index page under two URIs */
	static int data = 1234567;
	shttpd_register_uri(ctx, "/main", &show_index, (void*)&data);

	/* Register a callback on wildcard URI */
	shttpd_register_uri(ctx, "/users/*/", &show_users, NULL);

	/* Show how to use password protection */
	shttpd_register_uri(ctx, "/secret", &show_secret, NULL);
	shttpd_set_option(ctx, "protect", "/secret=passfile");

	/* Show how to use stateful big data transfer */
	shttpd_register_uri(ctx, "/huge", &show_huge, NULL);

	/* Register URI for file upload */
	shttpd_register_uri(ctx, "/post", &show_post, NULL);

	/* Register SSI callbacks */
	shttpd_register_ssi_func(ctx, "true", ssi_test_true, NULL);
	shttpd_register_ssi_func(ctx, "false", ssi_test_false, NULL);
	shttpd_register_ssi_func(ctx, "shttpd_version", ssi_shttpd_version, NULL);
	shttpd_register_ssi_func(ctx, "http_header", ssi_http_header, NULL);

	MUTEXINIT(&JSONMSG_lock);
	JSONMSG_lst = calloc(sizeof(json_object *), JSONMSG_NUM);
	JSONMSG_cnt = JSONMSG_idx = 0;
	MUTEXLOCK(&JSONMSG_lock);
	json_restore();
	MUTEXUNLOCK(&JSONMSG_lock);

	/* wkliang:20090518 - Register AJAX function */
	shttpd_register_uri(ctx, "/json/chat", &user_proc, (void*)json_chat);
	shttpd_register_uri(ctx, "/json/fetchpage", &user_proc, (void*)json_fetchpage);
	shttpd_register_uri(ctx, "/tctchat", &user_proc, (void*)tctchat);
	shttpd_register_uri(ctx, "/quickstart", &user_proc, (void*)quickstart);
	shttpd_register_uri(ctx, "/chat/xml", &user_proc, (void*)chat_xml);
	shttpd_register_uri(ctx, "/chat/getcolor", &user_proc, (void*)chat_getcolor);

	shttpd_handle_error(ctx, 404, show_404, NULL);

	// blog_init
	g_mpool = tcmpoolnew();
	g_scriptname = g_scriptprefix = g_scriptpath = "/blog";
	g_docroot = ctx->options[OPT_ROOT];
	if (!g_docroot) g_docroot = "/";
	char *tmplpath = tcmpoolpushptr(g_mpool,
				tcsprintf("%s%s.tmpl", g_docroot, g_scriptprefix));
	MY_DEBUG("tmpl: %s\n", tmplpath);
	g_tmpl = tcmpoolpush(g_mpool, tctmplnew(), (void (*)(void *))tctmpldel);
	if (!tctmplload2(g_tmpl, tmplpath)) {
		MY_DEBUG("The template file is missing.\n");
	} else {
		g_database = tctmplconf(g_tmpl, "database");
		if (!g_database) g_database = "promenade.tct";
		g_password = tctmplconf(g_tmpl, "password");
		if (g_password){
			g_users = tcmpoolpushmap(g_mpool, tcmapnew2(TINYBNUM));
			readpasswd();
		}
		g_upload = tctmplconf(g_tmpl, "upload");
		g_uploadpub = NULL;
		if (g_upload && *g_upload != '\0') {
			if (!strchr(g_upload, '/')) {
				g_uploadpub = g_upload;
				g_upload = tcmpoolpushptr(g_mpool,
					tcsprintf("%s/%s", g_docroot, g_uploadpub));
			} else {
				char *rpath = tcmpoolpushptr(g_mpool, tcrealpath(g_upload));
				if (rpath) {
					if (tcstrfwm(rpath, g_docroot)) {
						int plen = strlen(g_docroot);
						if(rpath[plen] == '/')
							g_uploadpub = rpath + plen;
					}
				}
			}
		}
		MY_DEBUG("g_upload: %s\n", g_upload ? g_upload : "NULL" );
		const char *rp = tctmplconf(g_tmpl, "recvmax");
		g_recvmax = rp ? tcatoix(rp) : INT_MAX;
		MY_DEBUG("g_recvmax: %lld\n", g_recvmax);
		g_mimerule = tctmplconf(g_tmpl, "mimerule");
		if(!g_mimerule) g_mimerule = "auto";
		g_title = tctmplconf(g_tmpl, "title");
		if(!g_title) g_title = "Tokyo Promenade";
		rp = tctmplconf(g_tmpl, "searchnum");
		g_searchnum = tclmax(rp ? tcatoi(rp) : 10, 1);
		rp = tctmplconf(g_tmpl, "listnum");
		g_listnum = tclmax(rp ? tcatoi(rp) : 10, 1);
		rp = tctmplconf(g_tmpl, "feedlistnum");
		g_feedlistnum = tclmax(rp ? tcatoi(rp) : 10, 1);
		rp = tctmplconf(g_tmpl, "filenum");
		g_filenum = tclmax(rp ? tcatoi(rp) : 10, 1);
		rp = tctmplconf(g_tmpl, "sidebarnum");
		g_sidebarnum = tclmax(rp ? tcatoi(rp) : 0, 0);
		g_commentmode = tctmplconf(g_tmpl, "commentmode");
		if(!g_commentmode) g_commentmode = "";
		g_updatecmd = tctmplconf(g_tmpl, "updatecmd");
		if(!g_updatecmd) g_updatecmd = "";
		rp = tctmplconf(g_tmpl, "sessionlife");
		g_sessionlife = tclmax(rp ? tcatoi(rp) : 0, 0);
		g_frontpage = tctmplconf(g_tmpl, "frontpage");
		if(!g_frontpage) g_frontpage = "/";

		shttpd_register_uri(ctx, "/", &user_proc, (void*)dosession);
		shttpd_register_uri(ctx, g_scriptname, &user_proc, (void*)dosession);
#if 0
		TCMAP *conf = g_tmpl->conf;
		tcmapiterinit(conf);
		while((rp = tcmapiternext2(conf)) != NULL){
			const char *pv = tcmapiterval2(rp);
			char *name = tcmpoolpushptr(g_mpool, tcsprintf("TP_%s", rp));
			tcstrtoupper(name);
			setenv(name, pv, 1);
		}
		// shttpd_register_uri(ctx, "/archive/*", &archive_proc, NULL);
#endif
	}
}
