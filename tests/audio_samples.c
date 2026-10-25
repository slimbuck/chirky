#include "../src/asset_store.h"
#include "../src/platform/linux/audio_mixer.c"
#include <assert.h>

static struct chirky_asset_view ready(struct asset_store *store,chirky_asset asset)
{
    uint64_t began=audio_milliseconds();
    while(asset_store_state(store,asset)==CHIRKY_ASSET_LOADING) {
        assert(audio_milliseconds()-began<3000);
        struct timespec delay={0,1000000};nanosleep(&delay,NULL);
    }
    assert(asset_store_state(store,asset)==CHIRKY_ASSET_READY);
    return asset_store_view(store,asset);
}

int main(void)
{
    const char *names[]={"jump","dash","shard","checkpoint","death","win"};
    struct asset_store *store=asset_store_create();assert(store);
    for(size_t i=0;i<sizeof(names)/sizeof(*names);i++) {
        char path[256];snprintf(path,sizeof(path),"games/phosphor-run/assets/%s.wav",names[i]);
        chirky_asset blob=asset_store_request(store,path,CHIRKY_ASSET_BLOB);
        chirky_asset sound=asset_store_request(store,path,CHIRKY_ASSET_SOUND);
        struct chirky_asset_view raw=ready(store,blob),pcm=ready(store,sound);
        /* These shipped fixtures have the generator's canonical 44-byte WAV
           header. Compare against original file bytes, not decoder output. */
        const unsigned char *wav=raw.data;
        assert(raw.size>=44 && !memcmp(wav,"RIFF",4) && !memcmp(wav+8,"WAVEfmt ",8));
        assert(!memcmp(wav+36,"data",4) && raw.size==pcm.size+44);
        assert(pcm.rate==48000 && pcm.channels==2 && pcm.size%4==0);
        size_t frames=pcm.size/4;
        unsigned char *output=calloc(frames+1,4);assert(output);
        struct audio_voice voices[AUDIO_VOICES]={0};unsigned count=0;
        struct audio_command sample={pcm.data,frames,pcm.rate,pcm.channels,100};
        for(size_t frame=0;frame<frames;frame++) {
            if(frame%240==0)for(unsigned repeat=0;repeat<8;repeat++)audio_add_voice(voices,&count,sample);
            assert(count==1);
            audio_mix(voices,&count,output+frame*4,1);
        }
        assert(!count && !memcmp(output,wav+44,pcm.size));
        for(size_t j=pcm.size;j<pcm.size+4;j++)assert(output[j]==0);
        printf("Audio: %s stays bit-exact under repeat requests (%zu frames at %u Hz).\n",names[i],frames,pcm.rate);
        free(output);asset_store_release(store,sound);asset_store_release(store,blob);
    }
    const char *combat[]={"punch","kick","block","miss"};
    for(size_t i=0;i<4;i++) {
        char path[256];snprintf(path,sizeof(path),"games/circuit-clash/assets/sounds/%s.wav",combat[i]);
        chirky_asset sound=asset_store_request(store,path,CHIRKY_ASSET_SOUND);
        struct chirky_asset_view pcm=ready(store,sound);
        assert(pcm.rate==24000 && pcm.channels==1 && pcm.size>0);
        size_t frames=pcm.size/2;
        unsigned char *output=calloc(frames*2+1,4);assert(output);
        struct audio_voice voices[AUDIO_VOICES]={0};unsigned count=0;
        struct audio_command sample={pcm.data,frames,pcm.rate,pcm.channels,70};
        audio_add_voice(voices,&count,sample);audio_mix(voices,&count,output,(unsigned)(frames*2+1));
        assert(!count);
        for(size_t f=0;f<frames;f++) {
            int16_t actual;memcpy(&actual,output+f*8,2);
            assert(actual==audio_sample(&sample,f,0)*70/100);
            assert(!memcmp(output+f*8,output+f*8+2,2));
        }
        free(output);asset_store_release(store,sound);
    }
    asset_store_destroy(store);
    return 0;
}
