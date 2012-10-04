typedef int (*check_init)(void**);
typedef int (*check_run)(void*);
typedef int (*check_post)(void*);
typedef int (*check_clean)(void*);
struct judge_check_info{
    char sopath[PATH_MAX + 1];
    void *sohandle;
    check_init init_fn;
    check_run run_fn;
    check_post post_fn;
    check_clean clean_fn;

    void *private;
};

struct judge_proc_info{
    int state;
    char path[PATH_MAX + 1];
    char name[NAME_MAX + 1];
    unsigned long pid; 
    unsigned long task;
    struct judge_check_info *check_info;

    unsigned long timelimit;
    unsigned long memlimit;
    unsigned long runtime;
    unsigned long peakmem;
};

struct judge_proc_info* judge_proc_create(char *path,char *sopath,unsigned long timelimit,unsigned long memlimit);
int judge_proc_free(struct judge_proc_info *proc_info);
static int proc_protect(struct judge_proc_info *proc_info);
int judge_proc_run(struct judge_proc_info *proc_info);
