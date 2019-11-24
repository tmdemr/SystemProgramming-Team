#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <setjmp.h>

#define MAX_CANON 256
#define QUIT_STRING "q" // q가 입력되면 종료한다.
#define BLANK_STRING " " // 공백 문자
#define PROMPT_STRING "[SHELL>> " 
#define BACK_SYMBOL '&' // 백그라운드에서 실행되도록 할 때

void FileRedirect(char *s, int in, int out); //파일 재지향
void Command_exe(char *); //시스템 명령 실행
int ArgvPointer(const char *s, const char *delimiters, char ***argvp); //문자열 포인터배열로 정의
int SetSignal(struct sigaction *def, sigset_t *mask, void(*handler)(int)); //시그널 설정
int FindRedirectIn(char *cmd); // <을 찾고 분석한다.
int FindRedircetOut(char *cmd); // >을 찾고 분석한다.
static void perror_exit(char *s);

int desc[2];

static sigjmp_buf ToPrompt;
static volatile sig_atomic_t IndexJump = 0;

static void jumphd(int signalnum) {
 	if (!IndexJump) 
		return;
 	IndexJump = 0;
 	siglongjmp(ToPrompt, 1);
}

int main(void) {
 	pid_t childpid;
 	char inbuf[MAX_CANON];
 	int len;
 	sigset_t blockmask;
 	struct sigaction defhandler;
 	char *backp;
 	int inbackground;
 	char **str;
 	int tcnt = 0;
 	char pipebuf[101];
 	int j, k;
 	int str_len;
 	int hcnt = 0, st = 0;
 	int recordok = 0;

 	pipe(desc);

 	if (SetSignal(&defhandler, &blockmask, jumphd) == -1) {
  		perror("Failed to set up shell signal handling");
  		return 1;
 	}

 	if (sigprocmask(SIG_BLOCK, &blockmask, NULL) == -1) {
  		perror("Failed to block signals");
  		return 1;
 	}
 	alarm(600);
/*10분마다 사용시간을 출력하도록 함.*/
 	while (1) {
  		if ((sigsetjmp(ToPrompt, 1)) && (fputs("\n", stdout) == EOF))
   			continue;
  		IndexJump = 1;
  		if (fputs(PROMPT_STRING, stdout) == EOF)
   			continue;
  		if (fgets(inbuf, MAX_CANON, stdin) == NULL)
  			continue;
  		len = strlen(inbuf);
  		if (inbuf[len - 1] == '\n')
   			inbuf[len - 1] = 0;
  		if (strcmp(inbuf, QUIT_STRING) == 0)
   			break;
  		if ((backp = strchr(inbuf, BACK_SYMBOL)) == NULL)
   			inbackground = 0;
  		else {
   			inbackground = 1;
   			*backp = 0;
  		}
  		if (sigprocmask(SIG_BLOCK, &blockmask, NULL) == -1)
   			perror("Failed to block signals");
  		ArgvPointer(inbuf, " \t", &str);
              /*cd 명령어 부분 */
  		if (strcmp(str[0], "cd") == 0) {
   			chdir(str[1]);
   			system("pwd");
  			continue;
  		}
		if (strcmp(str[0], "exit") == 0) {
   			return 0;
  		}
               /* 자식 프로세스로부터 데이터를 받는다.(시작)*/
  		for (j = 0; j < 100; j++)
   			pipebuf[j] = '\0';

  		write(desc[1], " ", sizeof(char));
  		str_len = read(desc[0], pipebuf, 100);
  		pipebuf[str_len] = 0;

  		printf("[%s | %d] \n", pipebuf, str_len);
              /*자식 프로세스로부터 데이터를 받는다 (종료)*/


  		if ((childpid = fork()) == -1) {
   			perror("Failed to fork child to execute command");
  		}
  		else if (childpid == 0) {
   			if (inbackground && (setpgid(0, 0) == -1))
    				return 1;
   			if((sigaction(SIGINT,&defhandler,NULL)==-1)|| (sigaction(SIGQUIT, &defhandler, NULL) == -1) || (sigprocmask(SIG_UNBLOCK, &blockmask, NULL) == -1)) {
    				perror("Failed to set signal handling for command ");
    				return 1;
   			}
   			Command_exe(inbuf);
   			return 1;
  		}
  		if (sigprocmask(SIG_UNBLOCK, &blockmask, NULL) == -1)
   			perror("Failed to unblock signals");
  		if (!inbackground)
   			waitpid(childpid, NULL, 0);
  		while (waitpid(-1, NULL, WNOHANG) > 0);
 	}
 	return 0;
}
static void perror_exit(char *s) {
 	perror(s);
 	exit(1);
}

int ArgvPointer(const char *s, const char *delimiters, char ***argvp) { //문자열 포인터 배열로 정의한다.
/*delimiter를 구분자로 하여, 문자열 s로부터 argvp가 가리키는 곳에 인자 배열을 만듭니다. 내부적으로 strtok 함수를 호출하여 문자열에 대한 토큰 수를 계산한 뒤, 이 토큰수를 이용하여 배열의 크기를 할당하고 이어 각각의 토큰에 대한 포인터를 만드는 작업 등을 합니다.*/
 	int error;
 	int i;
 	int numtokens;
 	const char *snew;
 	char *t;

 	if ((s == NULL) || (delimiters == NULL) || (argvp == NULL)) {
  		errno = EINVAL;
  		return -1;
 	}
 	*argvp = NULL;
 	snew = s + strspn(s, delimiters);
 	if ((t = (char *)malloc(strlen(snew) + 1)) == NULL)//메모리 할당
 		 return -1;
 	strcpy(t, snew);
 	numtokens = 0;
 	if (strtok(t, delimiters) != NULL)
  		for (numtokens = 1; strtok(NULL, delimiters) != NULL; numtokens++);

 	if ((*argvp = malloc((numtokens + 1) * sizeof(char *))) == NULL) {
  		error = errno;
  		free(t);
  		errno = error;
  		return -1;
 	}
 	if (numtokens == 0)
  		free(t);
 	else {
  		strcpy(t, snew);
  		**argvp = strtok(t, delimiters);
  		for (i = 1; i < numtokens; i++)
   			*((*argvp) + i) = strtok(NULL, delimiters);
 	}
 	*((*argvp) + numtokens) = NULL;
 	return numtokens;
}
int FindRedirectIn(char *cmd) { // 재지향 기호 ‘<’ 를 찾아 처리하는 부분
 	int error;
 	int infd;
 	char *infile;

 	if ((infile = strchr(cmd, '<')) == NULL)
  		return 0;
 	*infile = 0;
 	infile = strtok(infile + 1, " \t");
 	if (infile == NULL)
  		return 0;
 	if ((infd = open(infile, O_RDONLY)) == -1)
  		return -1;
 	if (dup2(infd, STDIN_FILENO) == -1) {
  		error = errno;
  		close(infd);
  		errno = error;
  		return -1;
 	}
 	return close(infd);
}
int FindRedircetOut(char *cmd) { // 재지향 기호 ‘>’를 찾아 처리하는 부분
 	int error;
 	int outfd;
 	char *outfile;
 	if ((outfile = strchr(cmd, '>')) == NULL)
  		return 0;
 	*outfile = 0;
 	outfile = strtok(outfile + 1, " \t");
 	if (outfile == NULL)
  		return 0;
 	if ((outfd = open(outfile, O_WRONLY)) == -1)
  		return -1;
 	if (dup2(outfd, STDOUT_FILENO) == -1) {
  		error = errno;
  		close(outfd);
  		errno = error;
  		return -1;
 	}
 	return close(outfd);
}

void Command_exe(char *cmds) { //시스템 명령 실행
/*입력된 문자열들을 정렬한 후 각 문자열 부분마다 파이프와 자식프로세스를 생성한 뒤, 마지막 명력을 제외한 각 문자열의 표준 출력을 다음 명령의 표준 입력으로 재지향합니다. 이 재지향을 통해 부모의 출력을 자식이 받아 문자열을 처리할 수 있게 되어 있습니다.*/
 	int child;
 	int count;
 	int fds[2];
 	int i;
 	char **pipelist;
/*파이프라인 시작*/
 	count = ArgvPointer(cmds, "|", &pipelist);
 	if (count <= 0) {
  		fprintf(stderr, "Failed\n");
  		exit(1);
 	}
 	for (i = 0; i < count - 1; i++) {
  		if (pipe(fds) == -1)
   			perror_exit("Failed to create pipes");
  		else if ((child = fork()) == -1)
   			perror_exit("Failed to create process to run command");
  		else if (child) {
   			if (dup2(fds[1], STDOUT_FILENO) == -1)
    				perror_exit("Failed to connect pipeline");
   			if (close(fds[0]) || close(fds[1]))
    				perror_exit("Failed to close needed files");

   			FileRedirect(pipelist[i], i == 0, 0);
   			exit(1);
  		}
  		if (dup2(fds[0], STDIN_FILENO) == -1)
   			perror_exit("Failed to connect last component");
 		if (close(fds[0]) || close(fds[1]))
   			perror_exit("Failed to do final close");
 	}
 	FileRedirect(pipelist[i], i == 0, 1);
 	exit(1);
/*파이프라인 끝*/
}
void FileRedirect(char *s, int in, int out) { //파일 재지향
 	char **chargv;
 	char *pin;
 	char *pout;
 	int i, j;
 	if (in && ((pin = strchr(s, '<')) != NULL) && out && ((pout = strchr(s, '>')) != NULL) && (pin > pout)) {
  		if (FindRedirectIn(s) == -1) {
   			perror("Failed to redirect input");
   			return;
  		}
 	}

 	if (out && FindRedircetOut(s) == -1)
  		perror("Failed to redirect output");
 	else if (in && FindRedirectIn(s) == -1)
  		perror("failed to redirect input");
 	else if (ArgvPointer(s, " \t", &chargv) <= 0)
  		fprintf(stderr, "failed to parse command line\n");
 	else {
  		for (i = 0; chargv[i] != 0; i++) {
   			for (j = 0; chargv[i][j] != 0; j++) {
    				write(desc[1], &chargv[i][j], sizeof(char));
   			}
   			write(desc[1], " ", sizeof(char));
  		}
  		execvp(chargv[0], chargv);
  		perror("failed to execute command");

  		write(desc[1], "/5999", sizeof("/5999"));
 	}
 	exit(1);
}
/*재지향 끝*/
int SetSignal(struct sigaction *def, sigset_t *mask, void(*handler)(int)) { //시그널 설정
 	struct sigaction catch;
 	catch.sa_handler = handler;
 	def->sa_handler = SIG_DFL;
 	catch.sa_flags = 0;
 	def->sa_flags = 0;
 	if ((sigemptyset(&(def->sa_mask)) == -1) || (sigemptyset(&(catch.sa_mask)) == -1) || (sigaddset(&(catch.sa_mask),SIGINT) == -1) || (sigaddset(&(catch.sa_mask), SIGQUIT) == -1) || (sigaction(SIGINT, &catch,NULL) == -1) || (sigaction(SIGQUIT, &catch, NULL) == -1) || (sigemptyset(mask) == -1) || (sigaddset(mask, SIGINT) == -1) || (sigaddset(mask, SIGQUIT) == -1))
  	return -1;
 return 0;
}
