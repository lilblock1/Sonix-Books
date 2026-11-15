#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/system/ebook/ebooksearch.h"
#include "src/system/ebook/ebooktext.h"

/* Isolate the parser boundary. A real EPUB integration check also exercises
 * ZIP/OPF/XHTML parsing; these fixtures cover match positions and bounds. */
typedef struct {
 char text[8192]; ebook_block_t blocks[16]; ebook_span_t spans[32];
 uint32_t length, nblocks, nspans, weight; bool fail;
} chapter_t;
static chapter_t chapters[3];
static unsigned nchapter=3, opened, closed, loaded;
struct ebook { unsigned chapter; };
ebook_t *ebook_open(const char *path) {
 if(!path || !strcmp(path,"missing")) return NULL;
 opened++; return calloc(1,sizeof(ebook_t));
}
void ebook_close(ebook_t *b) { if(b) {closed++; free(b);} }
uint32_t ebook_spine_count(const ebook_t *b) {(void)b; return nchapter;}
uint32_t ebook_chapter_weight(ebook_t *b,uint32_t i) {(void)b; return chapters[i].weight;}
bool ebook_open_chapter(ebook_t *b,uint32_t i) {loaded++; b->chapter=i; return !chapters[i].fail;}
const char *ebook_chapter_text(const ebook_t *b) {return chapters[b->chapter].text;}
uint32_t ebook_block_count(const ebook_t *b) {return chapters[b->chapter].nblocks;}
const ebook_block_t *ebook_block(const ebook_t *b,uint32_t i) {
 return i<chapters[b->chapter].nblocks?&chapters[b->chapter].blocks[i]:NULL;
}
const ebook_span_t *ebook_span(const ebook_t *b,uint32_t i) {
 return i<chapters[b->chapter].nspans?&chapters[b->chapter].spans[i]:NULL;
}
static void block(unsigned chapter,unsigned type,const char *a,const char *b) {
 chapter_t *c=&chapters[chapter]; assert(c->nblocks<16);
 ebook_block_t *v=&c->blocks[c->nblocks++]; v->type=(uint8_t)type; v->first_span=c->nspans;
 const char *parts[]={a,b};
 for(unsigned i=0;i<2;i++) if(parts[i]) {
  size_t n=strlen(parts[i]); assert(c->length+n+1<=sizeof(c->text)&&c->nspans<32);
  ebook_span_t *span=&c->spans[c->nspans++];
  span->offset=c->length; span->length=(uint32_t)n; span->style=(uint8_t)i;
  memcpy(c->text+c->length,parts[i],n+1); c->length+=(uint32_t)n+1; v->span_count++;
 }
}
typedef struct {unsigned calls,cancel_after,progress;} state_t;
static bool cancel(void *user) {state_t *s=user; return ++s->calls>=s->cancel_after;}
static void progress(uint32_t chapter,uint32_t total,void *user) {
 state_t *s=user; assert(chapter==++s->progress&&total==nchapter);
}
static void scan(const char *query,ebooksearch_result_t *out) {
 unsigned before=opened,freed=closed;
 ebooksearch_run("fixture.epub",query,out,NULL,NULL,NULL);
 assert(opened==before+1&&closed==freed+1);
}
int main(void) {
 ebooksearch_result_t *out=malloc(sizeof(*out)); assert(out);
 for(unsigned i=0;i<3;i++) chapters[i].weight=1000;
 block(0,EBOOK_BLOCK_HEADING,"Opening",NULL);
 block(0,EBOOK_BLOCK_PARAGRAPH,"Alpha needle and another NEEDLE.",NULL);
 block(0,EBOOK_BLOCK_QUOTE,"Look for a sty","led PHRASE here.");
 block(0,EBOOK_BLOCK_IMAGE,"/needle.png",NULL);
 block(0,EBOOK_BLOCK_RULE,"needle",NULL);
 block(0,EBOOK_BLOCK_PARAGRAPH,"\xc3\x89MILE \xce\x91\xce\x92\xce\x93 \xd0\x90\xd0\x91\xd0\x92",NULL);
 chapters[1].fail=true;
 block(2,EBOOK_BLOCK_PARAGRAPH,"Final needle.",NULL);
 block(2,EBOOK_BLOCK_LIST_ITEM,"two \n\t   words and 100%_literal",NULL);
 block(2,EBOOK_BLOCK_PARAGRAPH,"across",NULL);
 block(2,EBOOK_BLOCK_PARAGRAPH,"blocks",NULL);
 block(2,EBOOK_BLOCK_PARAGRAPH,"banana",NULL);
 block(2,EBOOK_BLOCK_PARAGRAPH,"\xe7\x8c\xab\xe3\x81\xa8\xe7\x8a\xac\xe3\x80\x82\xe7\x8c\xab\xe3\x80\x82",NULL);
 scan("needle",out);
 assert(out->status==EBOOKSEARCH_OK&&out->count==3&&out->unreadable==1);
 assert(out->chapters_scanned==3&&!out->truncated);
 assert(out->hits[0].position.spine==0&&out->hits[0].position.block==1&&out->hits[0].position.offset==6);
 assert(out->hits[1].position.offset==25&&out->hits[0].match_bytes==6);
 assert(out->hits[2].position.spine==2&&strstr(out->hits[2].snippet,"Final needle"));
 scan("styled phrase",out);
 assert(out->count==1&&out->hits[0].position.block==2&&out->hits[0].position.offset==11);
 assert(out->hits[0].match_bytes==13&&strstr(out->hits[0].snippet,"styled PHRASE"));
 scan("two words",out); assert(out->count==1&&out->hits[0].match_bytes>strlen("two words"));
 scan(" \t NEEDLE \n",out); assert(out->count==3);
 scan("%_",out); assert(out->count==1);
 scan("across blocks",out); assert(!out->count);
 scan("ana",out); assert(out->count==2&&out->hits[0].position.offset==1&&out->hits[1].position.offset==3);
 scan("\xc3\xa9mile",out); assert(out->count==1);
 scan("\xce\xb1\xce\xb2",out); assert(out->count==1);
 scan("\xd0\xb0\xd0\xb1",out); assert(out->count==1);
 scan("\xe7\x8c\xab",out); assert(out->count==2&&out->hits[0].match_bytes==3&&out->hits[1].position.offset==12);
 scan("absent",out); assert(!out->count&&out->status==EBOOKSEARCH_OK);
 state_t state={0};
 ebooksearch_run("fixture.epub","needle",out,NULL,progress,&state); assert(state.progress==3);
 unsigned before=opened;
 ebooksearch_run("fixture.epub"," \t\r\n",out,NULL,NULL,NULL);
 assert(out->status==EBOOKSEARCH_EMPTY_QUERY&&opened==before);
 ebooksearch_run("fixture.epub",NULL,out,NULL,NULL,NULL); assert(out->status==EBOOKSEARCH_EMPTY_QUERY&&opened==before);
 char long_query[300]; memset(long_query,'a',sizeof(long_query)); long_query[299]=0;
 ebooksearch_run("fixture.epub",long_query,out,NULL,NULL,NULL); assert(out->status==EBOOKSEARCH_LONG_QUERY&&opened==before);
 long_query[97]=0;
 ebooksearch_run("fixture.epub",long_query,out,NULL,NULL,NULL); assert(out->status==EBOOKSEARCH_LONG_QUERY&&opened==before);
 ebooksearch_run("missing","needle",out,NULL,NULL,NULL); assert(out->status==EBOOKSEARCH_OPEN_FAILED);
 state=(state_t){.cancel_after=1};
 ebooksearch_run("fixture.epub","needle",out,cancel,NULL,&state); assert(out->cancelled&&!out->count&&opened==before);
 memset(chapters,0,sizeof(chapters)); nchapter=1;
 char text[8192]; size_t n=0;
 for(int i=0;i<120;i++) {memcpy(text+n,"\xe7\x8c\xab",3); n+=3;}
 memcpy(text+n,"needle",6); n+=6; memset(text+n,'x',500); n+=500; text[n]=0;
 block(0,EBOOK_BLOCK_PARAGRAPH,text,NULL); scan("needle",out);
 assert(out->count==1&&out->hits[0].position.offset==360);
 assert(!strncmp(out->hits[0].snippet,"... ",4)&&strstr(out->hits[0].snippet,"needle"));
 size_t size=strlen(out->hits[0].snippet); assert(size<EBOOKSEARCH_SNIPPET);
 for(size_t at=0;at<size;) assert(ebooktext_decode(out->hits[0].snippet,size,&at)!=0xfffd);
 memset(chapters,0,sizeof(chapters)); memset(text,'a',200); text[200]=0;
 block(0,EBOOK_BLOCK_PARAGRAPH,text,NULL); scan("a",out); assert(out->count==100&&out->truncated);
 memset(chapters,0,sizeof(chapters)); text[100]=0;
 block(0,EBOOK_BLOCK_PARAGRAPH,text,NULL); scan("a",out); assert(out->count==100&&!out->truncated);
 chapters[0].weight=EBOOKSEARCH_MAX_BYTES+1; unsigned previous_loads=loaded;
 scan("a",out); assert(out->truncated&&!out->count&&loaded==previous_loads);
 memset(chapters,0,sizeof(chapters)); memset(text,'x',sizeof(text)-1); text[sizeof(text)-1]=0;
 block(0,EBOOK_BLOCK_PARAGRAPH,text,NULL); state=(state_t){.cancel_after=10}; before=closed;
 ebooksearch_run("fixture.epub","absent",out,cancel,NULL,&state); assert(out->cancelled&&closed==before+1);
 size_t at=0; assert(ebooktext_decode("\xf0\x9f\x98\x80",4,&at)==0x1f600&&at==4);
 at=0; assert(ebooktext_decode("\xc3",1,&at)==0xfffd&&at==1);
 at=0; assert(ebooktext_decode("\xed\xa0\x80",3,&at)==0xfffd&&at==1);
 at=0; assert(ebooktext_decode("\xc0\xaf",2,&at)==0xfffd&&at==1);
 assert(opened==closed); free(out);
 puts("ebook search: styled spans, Unicode, exact offsets, snippets, limits, skipped chapters and cancellation passed");
 return 0;
}
