/* SPDX-License-Identifier: Apache-2.0
 * Bounded, layer-major prefill. Same GEMV dot/reduction as serial decoding;
 * only scheduling and weight-row cache reuse change. Included after helpers.
 * g_model_lock is held; no new MMU, NPU, model format or ISA requirement. */
static int prefill_batch(FILE *stream, uint64_t data_start,
    struct tensor_info_s *tensors, uint64_t tensor_count,
    struct tensor_info_s *embedding, const uint32_t *tokens,
    uint32_t first, unsigned batch, uint32_t capacity, uint32_t blocks,
    uint32_t heads, uint32_t kv_heads, size_t head_dim, unsigned rotary,
    int normal_rope, float epsilon, float frequency, unsigned mask,
    const uint8_t *base, size_t bytes, float *keys, float *values)
{
  size_t hidden=embedding->dimensions[0], qw=heads*head_dim, kv=kv_heads*head_dim;
  char name[GGUF_NAME_MAX];
  struct tensor_info_s *ff=find_tensor(tensors,tensor_count,"blk.0.ffn_gate.weight");
  if(!ff || batch<1 || batch>4 || first>=capacity || batch>capacity-first ||
     hidden>65536 || qw>65536 || kv>65536 || ff->dimension_count!=2 ||
     !ff->dimensions[1] || ff->dimensions[1]>262144)return -EINVAL;
  size_t ffn=ff->dimensions[1];
  size_t total=batch*(3*hidden+2*qw+2*kv+3*ffn)+hidden+2*capacity;
#ifdef CONFIG_A733_PRODUCT_V12
  float *arena=prefill_acquire(total);
#else
  float *arena=calloc(total,sizeof(float));
#endif
  if(!arena)return -ENOMEM;
  float *state=arena, *norm=state+batch*hidden, *query=norm+batch*hidden;
  float *key=query+batch*qw, *value=key+batch*kv, *context=value+batch*kv;
  float *projected=context+batch*qw, *gate=projected+batch*hidden;
  float *up=gate+batch*ffn, *swiglu=up+batch*ffn, *weight=swiglu+batch*ffn;
  float *scores=weight+hidden, *prob=scores+capacity;
  int rc=-EIO;
  for(unsigned b=0;b<batch;b++)
    if(read_dequantized_row(stream,data_start+embedding->offset,embedding,
                           tokens[first+b],state+b*hidden)<0)goto done;
  for(unsigned layer=0;layer<blocks;layer++) {
    if(generation_stopped()){rc=125;goto done;}
    struct tensor_info_s *an,*v,*q,*k,*vb,*qb,*kb,*o,*fn,*g,*u,*d;
#define PF_FIND(dst,suffix) do {snprintf(name,sizeof(name),"blk.%u." suffix,layer); \
    dst=find_tensor(tensors,tensor_count,name);} while(0)
    PF_FIND(an,"attn_norm.weight");PF_FIND(v,"attn_v.weight");
    PF_FIND(q,"attn_q.weight");PF_FIND(k,"attn_k.weight");
    PF_FIND(vb,"attn_v.bias");PF_FIND(qb,"attn_q.bias");PF_FIND(kb,"attn_k.bias");
    PF_FIND(o,"attn_output.weight");PF_FIND(fn,"ffn_norm.weight");
    PF_FIND(g,"ffn_gate.weight");PF_FIND(u,"ffn_up.weight");PF_FIND(d,"ffn_down.weight");
#undef PF_FIND
#define PF_SHAPE(t,x,y) ((t) && (t)->dimension_count==2 && (t)->dimensions[0]==(x) && (t)->dimensions[1]==(y))
    if(!PF_SHAPE(q,hidden,qw)||!PF_SHAPE(k,hidden,kv)||!PF_SHAPE(v,hidden,kv)||
       !PF_SHAPE(o,qw,hidden)||!PF_SHAPE(g,hidden,ffn)||!PF_SHAPE(u,hidden,ffn)||
       !PF_SHAPE(d,ffn,hidden)){rc=-EINVAL;goto done;}
#undef PF_SHAPE
    for(unsigned b=0;b<batch;b++)
      if(rmsnorm_vector(stream,data_start,an,state+b*hidden,hidden,epsilon,
                        weight,norm+b*hidden)<0){rc=-EIO;goto done;}
#define PF_MAT(t,in,out) do {rc=read_parallel_matvec_batch(stream,data_start+(t)->offset, \
    t,in,out,mask,base,bytes,batch);if(rc<0)goto done;} while(0)
    PF_MAT(v,norm,value);PF_MAT(q,norm,query);PF_MAT(k,norm,key);
    for(unsigned b=0;b<batch;b++) {
      unsigned pos=first+b;
      if((!(normal_rope&&!vb)&&add_f32_bias(stream,data_start,vb,value+b*kv,kv)<0)||
         (!(normal_rope&&!qb)&&add_f32_bias(stream,data_start,qb,query+b*qw,qw)<0)||
         (!(normal_rope&&!kb)&&add_f32_bias(stream,data_start,kb,key+b*kv,kv)<0)||
         (normal_rope?apply_normal_rope:apply_qwen2_rope)(query+b*qw,heads,head_dim,rotary,pos,frequency)<0||
         (normal_rope?apply_normal_rope:apply_qwen2_rope)(key+b*kv,kv_heads,head_dim,rotary,pos,frequency)<0)
        {rc=-EINVAL;goto done;}
      memcpy(keys+((size_t)layer*capacity+pos)*kv,key+b*kv,kv*sizeof(float));
      memcpy(values+((size_t)layer*capacity+pos)*kv,value+b*kv,kv*sizeof(float));
      for(unsigned head=0;head<heads;head++) {
        size_t src=(head/(heads/kv_heads))*head_dim, dst=b*qw+head*head_dim;
        if(!pos){memcpy(context+dst,value+b*kv+src,head_dim*sizeof(float));continue;}
        float maximum=-INFINITY;
        for(unsigned past=0;past<=pos;past++) {
          double dot=0;
          size_t at=((size_t)layer*capacity+past)*kv+src;
          for(size_t c=0;c<head_dim;c++)dot+=(double)(query[dst+c]*keys[at+c]);
          scores[past]=(float)dot*(1.0f/sqrtf((float)head_dim));
          maximum=fmaxf(maximum,scores[past]);
        }
        double sum=aipetllm_softmax_exp(pos+1,prob,scores,maximum);
        if(!isfinite(sum)||sum<=0){rc=-ERANGE;goto done;}
        float inv=(float)(1.0/sum);
        for(size_t c=0;c<head_dim;c++) {
          size_t at=(size_t)layer*capacity*kv+src+c;
          double acc=0;
          for(unsigned past=0;past<=pos;past++)acc+=(double)((prob[past]*inv)*values[at+past*kv]);
          context[dst+c]=(float)acc;
          if(pos==1)context[dst+c]=(prob[0]*inv)*values[at]+(prob[1]*inv)*values[at+kv];
        }
      }
    }
    PF_MAT(o,context,projected);
    for(size_t i=0;i<batch*hidden;i++)state[i]+=projected[i];
    for(unsigned b=0;b<batch;b++)
      if(rmsnorm_vector(stream,data_start,fn,state+b*hidden,hidden,epsilon,
                        weight,norm+b*hidden)<0){rc=-EIO;goto done;}
    PF_MAT(g,norm,gate);PF_MAT(u,norm,up);
    for(unsigned b=0;b<batch;b++)aipetllm_swiglu(ffn,swiglu+b*ffn,gate+b*ffn,up+b*ffn);
    PF_MAT(d,swiglu,projected);
    for(size_t i=0;i<batch*hidden;i++)state[i]+=projected[i];
#undef PF_MAT
  }
  rc=0;
done:
#ifdef CONFIG_A733_PRODUCT_V12
  prefill_release(arena);
#else
  free(arena);
#endif
  return rc;
}
