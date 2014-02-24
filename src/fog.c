#define _GNU_SOURCE

#define CONTPREFIX "fog"
#define DLYFREE_MAX 1024

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<limits.h>
#include<unistd.h>
#include<errno.h>
#include<sched.h>
#include<dirent.h>
#include<ftw.h>
#include<sys/mount.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<sys/mman.h>
#include<linux/btrfs.h>

#include"snap.h"
#include"timer.h"
#include"fog.h"

struct fog_cont{
    int cont_id;
};

/*static int delete_contdir(const char *path,const struct stat *st,
	int flag,struct FTW *ftwbuf);*/
static int chown_contdir(const char *path,const struct stat *st,
	int flag,struct FTW *ftwbuf);
static void handle_dlyfree(struct timer *timer);

static int last_cont_id = 0;
static struct timer *dlyfree_timer = NULL;
static int dlyfree_timer_flag = 0;
static int dlyfree_queue[DLYFREE_MAX];
static int dlyfree_queue_head = 0;
static int dlyfree_queue_tail = 0;

int fog_init(void){
    DIR *dirp;
    struct dirent *entry;
    struct stat st;

    if((dirp = opendir("container")) != NULL){
        while((entry = readdir(dirp)) != NULL){
	    if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")){
                continue;
            }
            if(snap_delete("container",entry->d_name)){
                closedir(dirp);
                return -1;
            }
        }

        closedir(dirp);
    }
    mkdir("container",0700);

    mkdir("cgroup",0700);
    mkdir("cgroup/cpu,cpuacct",0700);
    mkdir("cgroup/memory",0700);
    mount("cpu,cpuacct","cgroup/cpu,cpuacct","cgroup",MS_MGC_VAL,"cpu,cpuacct");
    mount("memory","cgroup/memory","cgroup",MS_MGC_VAL,"memory");

    if(stat("cgroup/cpu,cpuacct/cpuacct.stat",&st)){
	return -1;
    }
    if(stat("cgroup/memory/memory.memsw.max_usage_in_bytes",&st)){
	return -1;
    }
    
    if(nftw("snapshot",
                chown_contdir,16,FTW_DEPTH | FTW_PHYS | FTW_ACTIONRETVAL)){
	return -1;
    }

    if((dlyfree_timer = timer_alloc()) == NULL){
        return -1;
    }
    timer_set(dlyfree_timer,0,0);
    dlyfree_timer->alarm_handler = handle_dlyfree;
    dlyfree_timer_flag = 0;
    dlyfree_queue_head = 0;
    dlyfree_queue_tail = 0;

    return 0;
}
static int chown_contdir(
	const char *path,
	const struct stat *st,
	int flag,
	struct FTW *ftwbuf){

    if(flag != FTW_SL && 
	(flag != FTW_DP || strcmp(path,".") || strcmp(path,".."))){
	if(chown(path,FOG_CONT_UID,FOG_CONT_GID)){
	    return FTW_STOP;
	}
    }

    return FTW_CONTINUE;
}

int fog_cont_alloc(const char *snap){
    int id;
    char name[BTRFS_PATH_NAME_MAX + 1];
    char path[PATH_MAX + 1];
    struct stat st;

    last_cont_id++;
    id = last_cont_id;
    
    snprintf(name,BTRFS_PATH_NAME_MAX + 1,"%d",id);
    snprintf(path,PATH_MAX + 1,"snapshot/%s",snap);
    if(snap_create(path,"container",name)){
	return -1;
    }

    snprintf(path,PATH_MAX + 1,"cgroup/cpu,cpuacct/%s_%d",CONTPREFIX,id);
    rmdir(path);
    if(mkdir(path,0700)){
	return -1;
    }
    strncat(path,"/cpuacct.stat",PATH_MAX);
    if(stat(path,&st)){
	return -1;
    }

    snprintf(path,PATH_MAX + 1,"cgroup/memory/%s_%d",CONTPREFIX,id);
    rmdir(path);
    if(mkdir(path,0700)){
	return -1;
    }
    strncat(path,"/memory.memsw.limit_in_bytes",PATH_MAX);
    if(stat(path,&st)){
	return -1;
    }

    return id;
}
int fog_cont_set(int id,unsigned long memlimit){
    char path[PATH_MAX + 1];
    FILE *f;

    snprintf(path,PATH_MAX + 1,
            "cgroup/memory/%s_%d/memory.limit_in_bytes",CONTPREFIX,id);
    if((f = fopen(path,"w")) == NULL){
	return -1;
    }
    fprintf(f,"%lu",memlimit);
    fclose(f);

    snprintf(path,PATH_MAX + 1,
            "cgroup/memory/%s_%d/memory.memsw.limit_in_bytes",CONTPREFIX,id);
    if((f = fopen(path,"w")) == NULL){
	return -1;
    }
    fprintf(f,"%lu",memlimit);
    fclose(f);

    return 0;
}
int fog_cont_reset(int id){
    char path[PATH_MAX + 1];

    snprintf(path,PATH_MAX + 1,"cgroup/cpu,cpuacct/%s_%d",CONTPREFIX,id);
    if(rmdir(path)){
	return -1;
    }
    if(mkdir(path,0700)){
	return -1;
    }

    snprintf(path,PATH_MAX + 1,"cgroup/memory/%s_%d",CONTPREFIX,id);
    if(rmdir(path)){
	return -1;
    }
    if(mkdir(path,0700)){
	return -1;
    }
    
    return 0;
}
/*static int delete_contdir(
	const char *path,
	const struct stat *st,
	int flag,
	struct FTW *ftwbuf){

    if(flag == FTW_DP){
	if(strcmp(path,".") || strcmp(path,"..")){
	    if(rmdir(path)){
		return FTW_STOP;
	    }
	}
    }else{
	if(unlink(path)){
	    return FTW_STOP;   
	}
    }

    return FTW_CONTINUE;
}*/

int fog_cont_attach(int id){
    pid_t pid;
    char path[PATH_MAX + 1];
    FILE *f;

    pid = getpid();
    
    snprintf(path,PATH_MAX + 1,"cgroup/cpu,cpuacct/%s_%d/tasks",CONTPREFIX,id);
    if((f = fopen(path,"w")) == NULL){
	return -1;
    }
    fprintf(f,"%d",pid);
    fclose(f);
    
    snprintf(path,PATH_MAX + 1,"cgroup/memory/%s_%d/tasks",CONTPREFIX,id);
    if((f = fopen(path,"w")) == NULL){
	return -1;
    }
    fprintf(f,"%d",pid);
    fclose(f);
    
    snprintf(path,PATH_MAX + 1,"container/%d",id);
    if(chroot(path)){
	return -1;
    }
    chdir("/");

    if(setgid(FOG_CONT_GID) || setuid(FOG_CONT_UID)){
	return -1;
    }

    return 0;
}

int fog_cont_free(int id){
    char state;
    char name[BTRFS_PATH_NAME_MAX + 1];
    char path[PATH_MAX + 1];

    snprintf(name,BTRFS_PATH_NAME_MAX + 1,"%d",id);
    if(snap_delete("container",name)){
        printf("  error 1\n");

	state = 0;
	goto err;
    }

    snprintf(path,PATH_MAX + 1,"cgroup/cpu,cpuacct/%s_%d",CONTPREFIX,id);
    if(rmdir(path)){
        printf("  error 2\n");

	state = 1;
	goto err;
    }
    snprintf(path,PATH_MAX + 1,"cgroup/memory/%s_%d",CONTPREFIX,id);
    if(rmdir(path)){
        printf("  error 3\n");

	state = 2;
	goto err;
    }

    return 0;

err:

    dlyfree_queue[dlyfree_queue_tail] = (id << 2) | state;
    dlyfree_queue_tail = (dlyfree_queue_tail + 1) % DLYFREE_MAX;
    if(dlyfree_timer_flag == 0){
	timer_set(dlyfree_timer,1,2);
	dlyfree_timer_flag = 1;
    }

    return 0;
}
static void handle_dlyfree(struct timer *timer){
    int old_tail;
    int id;
    char state;
    char name[BTRFS_PATH_NAME_MAX + 1];
    char path[PATH_MAX + 1];

    printf("  delay free\n");

    old_tail = dlyfree_queue_tail;
    while(dlyfree_queue_head != old_tail){
	id = dlyfree_queue[dlyfree_queue_head] >> 2;
	state = dlyfree_queue[dlyfree_queue_head] & 3;

	if(state <= 0){
	    snprintf(name,BTRFS_PATH_NAME_MAX + 1,"%d",id);
	    if(snap_delete("container",name)){
		printf("  error 4\n");
		goto err;
	    }
	}
	if(state <= 1){
	    snprintf(path,PATH_MAX + 1,"cgroup/cpu,cpuacct/%s_%d",
		    CONTPREFIX,id);
	    if(rmdir(path)){
		printf("  error 5\n");
		goto err;
	    }
	}
	if(state <= 2){
	    snprintf(path,PATH_MAX + 1,"cgroup/memory/%s_%d",CONTPREFIX,id);
	    if(rmdir(path)){
		printf("  error 6\n");
		goto err;
	    }
	}

	goto end;

err:
    
	dlyfree_queue[dlyfree_queue_tail] = (id << 2) | state;
	dlyfree_queue_tail = (dlyfree_queue_tail + 1) % DLYFREE_MAX;

end:

	dlyfree_queue_head = (dlyfree_queue_head + 1) % DLYFREE_MAX;
    }
    
    if(dlyfree_queue_head == dlyfree_queue_tail){
	timer_set(dlyfree_timer,0,0);
	dlyfree_timer_flag = 0;
    }
}

/*
int fog_cont_stat(int id,struct cont_stat *stat){
    char path[PATH_MAX + 1]; 
    FILE *f;

    snprintf(path,PATH_MAX + 1,"cgroup/cpu,cpuacct/%s_%d/cpuacct.stat",
	    CONTPREFIX,id);
    if((f = fopen(path,"r")) == NULL){
	return -1;
    }
    fscanf(f,"user %lu\n",&stat->utime);
    fscanf(f,"system %lu\n",&stat->stime);
    fclose(f);
    stat->utime *= 1000UL / sysconf(_SC_CLK_TCK);
    stat->stime *= 1000UL / sysconf(_SC_CLK_TCK);
    
    snprintf(path,PATH_MAX + 1,
	    "cgroup/memory/%s_%d/memory.memsw.max_usage_in_bytes",
	    CONTPREFIX,id);
    if((f = fopen(path,"r")) == NULL){
	return -1;
    }
    fscanf(f,"%lu",&stat->memory);
    fclose(f);

    snprintf(path,PATH_MAX + 1,
	    "cgroup/memory/%s_%d/memory.kmem.max_usage_in_bytes",
	    CONTPREFIX,id);
    if((f = fopen(path,"r")) == NULL){
	return -1;
    }
    fscanf(f,"%lu",&stat->memory);
    fclose(f);

    return 0;
}
*/