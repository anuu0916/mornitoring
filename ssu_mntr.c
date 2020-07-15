#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 255
#endif

#define BUFFER_SIZE 512

char checkDir[PATH_MAX] = "check"; //check 디렉토리
char trashDir[PATH_MAX] = "trash"; //trash 디렉토리
char filesDir[PATH_MAX] = "files"; //files 디렉토리
char infoDir[PATH_MAX] = "info"; //info 디렉토리
char logpath[PATH_MAX]; //log.txt 파일 경로
char saved_path[PATH_MAX]; //소스파일이 있는 경로
struct timeval begin_t, end_t; //시간 계산 변수
struct checkinfo{ //모니터링에 필요한 구조체
	char d_name[BUFFER_SIZE];
	time_t mtime;
};

int daemon_init(void);
void prompt(void);
void print_tree(char *dirpath, int level, int p_end);
void ssu_mntr(char *dirpath);
void checkfile(char *dirpath, struct checkinfo *namelist, struct checkinfo *newnamelist, int filenum, int newfilenum, FILE *fp);
void do_delete(char **input_token, int has_endtime);
void do_recover(char **input_token);
void remove_dir(char *oldpath);
off_t get_infoDir_size(off_t dirsize);
void get_check_info(char *dirpath, int *totalnum, struct checkinfo *ch, time_t *newest);
void ssu_runtime();
void print_help();

int main(void)
{
	gettimeofday(&begin_t, NULL);
	FILE *fp;
	pid_t pid;

	getcwd(saved_path, BUFFER_SIZE); //현재 작업 디렉토리 저장

	if(access(checkDir, F_OK)<0){ //check 디렉토리가 존재하지 않으면 생성
		mkdir(checkDir, 0755);
	}
	chdir(checkDir);
	getcwd(checkDir, BUFFER_SIZE); //check 디렉토리 절대경로 저장
	chdir(saved_path); //작업 디렉토리 복귀

	if(access(trashDir, F_OK)<0){ //trash 디렉토리가 존재하지 않으면 생성
		mkdir(trashDir, 0755);
	}
	chdir(trashDir);
	getcwd(trashDir, BUFFER_SIZE); //trash 디렉토리 절대경로 저장
	
	chdir(saved_path); //작업 디렉토리 복귀

	if((fp = fopen("log.txt", "a+")) == NULL){ //log.txt file open
		fprintf(stderr, "fopen error for %s\n", "log.txt");
		exit(1);
	}
	fclose(fp);
	sprintf(logpath, "%s/%s", saved_path, "log.txt"); //log파일 절대경로 저장
	

	//trash 디렉토리 서브 디렉토리 생성
	chdir(trashDir);
	if(access(filesDir, F_OK)<0){ //files 디렉토리가 존재하지 않으면 생성
		mkdir(filesDir, 0755);
	}
	chdir(filesDir);
	getcwd(filesDir, BUFFER_SIZE); //files 디렉토리 절대경로 저장
	chdir(trashDir); //작업 디렉토리 복귀
	
	if(access(infoDir, F_OK)<0){ //info 디렉토리가 존재하지 않으면 생성
		mkdir(infoDir, 0755);
	}
	chdir(infoDir);
	getcwd(infoDir, BUFFER_SIZE); //info 디렉토리 절대경로 저장
	chdir(saved_path); //작업 디렉토리 복귀
	
	if((pid = fork())<0){ //자식 프로세스 생성
		fprintf(stderr, "fork error\n"); //에러 처리
		exit(1);
	}
	else if(pid == 0){ //자식 프로세스에서
		if(daemon_init() < 0){ //디몬 프로세스 생성
			fprintf(stderr, "daemon_init failed\n"); //에러 처리
			exit(1);
		}
	}
	

	prompt(); //프롬프트 출력

}

void ssu_mntr(char *dirpath){ //모니터링 함수
	FILE *fp;
	char filepath[BUFFER_SIZE];
	time_t intertime;
	int i;
	int count;
	int filenum=0, newfilenum=0;
	struct tm *timeinfo;
	struct dirent **namelist, **dirnamelist;
	struct stat statbuf, newstatbuf; //stat 구조체
	time_t rawtime;
	time_t newest=0, newnewest=0;
	char tmp[BUFFER_SIZE];
	struct checkinfo ch1[BUFFER_SIZE], ch2[BUFFER_SIZE];

	while(1){
		chdir(saved_path);
		if((fp = fopen(logpath, "a+")) == NULL){ //log.txt file open
			fprintf(stderr, "fopen error for %s\n", "log.txt");
			exit(1);
		}
		chdir(dirpath);

		filenum=0; //파일 개수 초기화
		stat(dirpath, &statbuf);
		newest = statbuf.st_mtime; //디렉토리 수정 시간 저장
		get_check_info(dirpath, &filenum, ch1, &newest); //check 디렉토리의 정보 저장

		sleep(1); //변화 감지 위한 간극

		stat(dirpath, &statbuf);
		newfilenum=0; //파일 개수 초기화
		newnewest = statbuf.st_mtime; //디렉토리 수정 시간 저장
		get_check_info(dirpath, &newfilenum, ch2, &newnewest); //check 디렉토리의 새 정보 저장
		
		if(newest != newnewest) //수정 시간이 다를 경우
			checkfile(dirpath, ch1, ch2, filenum, newfilenum, fp); //log 작성
		

		fclose(fp);
	}
	

	return;	
}

void get_check_info(char *dirpath, int *totalnum, struct checkinfo *ch, time_t *newest){
	struct stat statbuf;
	struct dirent **namelist;
	int filenum;
	int i,j;
	
	chdir(dirpath);

	if((filenum = scandir(".", &namelist, NULL, alphasort)) == -1){ //현재 디렉토리 scan
		fprintf(stderr, "scandir error\n");
		return;
	}

	for(i=0; i<filenum; i++){
		if(namelist[i]->d_name[0] == '.') //., .., .swp 파일 거르기
			continue;

		stat(namelist[i]->d_name, &statbuf);
		
		if(*newest < statbuf.st_mtime){ //가장 최신 수정시간으로 갱신
			*newest = statbuf.st_mtime;
		}

		if(S_ISDIR(statbuf.st_mode)){ //디렉토리일 때 재귀함수
			get_check_info(namelist[i]->d_name, totalnum, ch, newest);
			chdir(".."); //현재 디렉토리로 복귀
		}
		
		strcpy(ch[*totalnum].d_name, namelist[i]->d_name); //ch 구조체에 파일 이름 저장
		ch[(*totalnum)++].mtime = statbuf.st_mtime; //ch 구조체에 수정 시간 저장

	}
	return;

}

void checkfile(char *dirpath, struct checkinfo *namelist, struct checkinfo *newnamelist, int filenum, int newfilenum, FILE *fp){
	struct tm *timeinfo;
	time_t rawtime;
	char tmp[BUFFER_SIZE];
	char path[PATH_MAX];
	char *fname;
	int i,j;

	memset(tmp, 0, BUFFER_SIZE);

	if(newfilenum != filenum){ //파일 개수가 다를 때 (생성 or 삭제)
		for(j=0, i=0; i<filenum && j<newfilenum; i++, j++){
			if(strcmp(namelist[i].d_name, newnamelist[j].d_name) != 0){ //파일 이름이 다르면
				if(filenum < newfilenum){ //파일 생성 시
					time(&rawtime);
					timeinfo = localtime(&rawtime);
					strftime(tmp, BUFFER_SIZE, "%F %H:%M:%S", timeinfo);
					fprintf(fp, "[%s][create_%s]\n", tmp, newnamelist[j].d_name);
				}
				else if(filenum > newfilenum){ //파일 삭제 시
					time(&rawtime);
					timeinfo = localtime(&rawtime);
					strftime(tmp, BUFFER_SIZE, "%F %H:%M:%S", timeinfo);
					fprintf(fp, "[%s][delete_%s]\n", tmp, namelist[i].d_name);
				}
				break;
			}
		}
		if(filenum>newfilenum && i==newfilenum){ //마지막 파일이 삭제되었을 때
			time(&rawtime);
			timeinfo = localtime(&rawtime);
			strftime(tmp, BUFFER_SIZE, "%F %H:%M:%S", timeinfo);
			fprintf(fp, "[%s][delete_%s]\n", tmp, newnamelist[filenum-1].d_name);
		}
		else if(filenum<newfilenum && j==filenum){ //마지막 파일이 생성되었을 때
			time(&rawtime);
			timeinfo = localtime(&rawtime);
			strftime(tmp, BUFFER_SIZE, "%F %H:%M:%S", timeinfo);
			fprintf(fp, "[%s][create_%s]\n", tmp, newnamelist[newfilenum-1].d_name);
		}
		return;
	}
	else{ //파일 개수가 같을 때 (파일 수정)
		for(i=0; i<newfilenum; i++){
			if(namelist[i].mtime != newnamelist[i].mtime){
				time(&rawtime);
				timeinfo = localtime(&rawtime);
				strftime(tmp, BUFFER_SIZE, "%F %H:%M:%S", timeinfo);
				fprintf(fp, "[%s][modify_%s]\n", tmp, newnamelist[i].d_name);
			}
		}
	}

	return;
}

void prompt(void){
	char input[BUFFER_SIZE];
	char *input_token[32] = {0};
	char *filepath[32];
	char rOption;
	int c;
	int i=0;
	
	while(1){
		chdir(saved_path);
		memset(input_token, 0, 32);
		memset(input, 0, BUFFER_SIZE);	
		printf("20182629>"); //프롬프트 출력

		fgets(input, BUFFER_SIZE, stdin); //line buffer로 명령어 받음
		input[strlen(input)-1] = 0; //개행문자 제거

		if(!strncmp(input, "\0", 1)) //개행문자만 입력했을 경우 프롬프트 재출력
			continue;

		//띄어쓰기 기준으로 토큰 분리
		i=0;
		char *p = strtok(input, " ");
		while(p != NULL){
			input_token[i++] = p;
			p = strtok(NULL, " ");
		}
		
		//명령어 수행
		if(!strcmp(input_token[0], "delete")){
			if(i<2){
				printf("uasage : delete [FILENAME] [END_TIME] [OPTION]\n");
				continue;
			}
			if(i>4 && !strcmp(input_token[4], "-r")){
				printf("Delete [y/n]? ");
				scanf("%c", &rOption);
				getchar();
				if(rOption == 'n')
					continue;
				else if(rOption == 'y'){
					do_delete(input_token, i);
					continue;
				}
				else{
					printf("wrong input\n");
					continue;
				}
			}

			do_delete(input_token, i);
		}
		else if(!strcmp(input_token[0], "tree")){
			printf("check");
			print_tree(checkDir, 1, 0);
		}
		else if(!strcmp(input_token[0], "recover")){
			if(i<2){
				printf("uasage : recover [FILENAME]\n");
				continue;
			}
			do_recover(input_token);
		}
		else if(!strcmp(input_token[0], "exit")){
			gettimeofday(&end_t, NULL);
			printf("ssu_mntr is closed.\n");
			ssu_runtime();
			exit(0);
		}
		else if(!strcmp(input_token[0], "help")){
			print_help();
		}
		else{
			print_help();
		}

	}

}

void print_help(){
	printf("Usage : 20182629>\n");
	printf(" delete [FILENAME] [END_TIME] [OPTION] : delete file at end time\n");
	printf(" delete [OPTION] -r                    : check delete or not\n");
	printf(" recover [FILENAME]                    : recover file\n");
	printf(" help                                  : print usage\n");

}

void do_delete(char **input_token, int has_endtime){
	FILE *fp;
	char oldpath[PATH_MAX];
	char newpath[PATH_MAX];
	char path[PATH_MAX];
	char filename[BUFFER_SIZE];
	char tmp[BUFFER_SIZE];
	char *fname;
	char *ptr;
	struct tm* timeinfo;
	struct tm* deletetime;
	struct dirent **namelist;
	struct stat statbuf; //stat 구조체
	time_t rawtime;
	int dtime, ptime;
	int filenum;
	pid_t pid;
	off_t dirsize;

	memset(oldpath, 0, PATH_MAX);
	memset(newpath, 0, PATH_MAX);
	memset(filename, 0, BUFFER_SIZE);
	
	chdir(checkDir);

	if(input_token[1][0] == '/'){ //절대경로일 경우
		fname = strrchr(input_token[1], '/');
		fname++; //파일명 저장
		strcpy(oldpath, input_token[1]); //현재 절대경로
		sprintf(newpath, "%s/%s", filesDir, fname); //trash로 옮길 절대경로
	}
	else if(input_token[1][0] == '.'){ //상대경로일 경우
		if(realpath(input_token[1], oldpath) == NULL){ //절대경로로 변환
			printf("존재하지 않는 파일입니다.\n");
			return; //에러메시지 출력 후 프롬프트 출력
		}
		fname = strrchr(input_token[1], '/');
		fname++; //파일명 저장
		sprintf(newpath, "%s/%s", filesDir, fname); //trash로 옮길 절대경로
	}
	else{ //파일명만 입력했을 경우
		if(realpath(input_token[1], oldpath) == NULL){ //절대경로로 변환
			printf("존재하지 않는 파일입니다.\n");
			return; //에러메시지 출력 후 프롬프트 출력
		}
		fname = input_token[1]; //파일명 저장
		sprintf(newpath, "%s/%s", filesDir, fname); //trash로 옮길 절대경로
	}

	if(has_endtime >= 3){ //endtime을 입력받았을 때
		time(&rawtime);
		deletetime = localtime(&rawtime); //삭제 시간을 저장할 구조체


		//연-월-일 토큰 분리 후 tm 구조체 세팅
		ptr = strtok(input_token[2], "-");
		deletetime->tm_year = atoi(ptr) - 1900;
		ptr = strtok(NULL, "-");
		deletetime->tm_mon = atoi(ptr) - 1;
		ptr = strtok(NULL, "-");
		deletetime->tm_mday = atoi(ptr);

		//시:분 토큰 분리 후 tm 구조체 세팅
		ptr = strtok(input_token[3], ":");
		deletetime->tm_hour = atoi(ptr);
		ptr = strtok(NULL, ":");
		deletetime->tm_min = atoi(ptr);
		
		mktime(deletetime); //삭제 시간 구조체 변수 세팅
	
		//삭제 시간 초단위 계산
		dtime = deletetime->tm_yday*86400 + deletetime->tm_hour*3600 + deletetime->tm_min*60;

		time(&rawtime);
		timeinfo = localtime(&rawtime); //현재 시간 구조체

		//현재 시간 초단위 계산
		ptime = timeinfo->tm_yday*86400 + timeinfo->tm_hour*3600 + timeinfo->tm_min*60;

		if((dtime-ptime)<0){ //현재보다 과거의 시간일 경우
			printf("잘못된 시간 입력입니다.\n");
			return;
		}
	}
	else{ //endtime을 입력받지 않았을 때
		dtime = 0;
		ptime = 0;
	}

	if((pid = fork()) < 0){ //자식 프로세스 생성
		fprintf(stderr, "fork error\n"); //에러 처리
		return;
	}
	else if(pid > 0){ //부모 프로세스는 프롬프트 출력
		return;
	}
	else{ //자식 프로세스에서 delete 진행
		sleep(dtime-ptime); //삭제 시간까지 대기

		chdir(infoDir);
		if((fp = fopen(fname, "a+")) == NULL){ //info 파일 생성
			fprintf(stderr, "fopen error for %s\n", filename);
			exit(1);
		}
		int filesize;
		int i=0;
		char i_fname[BUFFER_SIZE];
		fseek(fp, 0, SEEK_END);
		filesize = ftell(fp); //file size 구함
		if(filesize > 0){ //같은 이름의 파일이 이미 존재할 경우
			while(filesize != 0){ //같은 파일이 없을 때까지
				i++;
				memset(i_fname, 0, BUFFER_SIZE);
				sprintf(i_fname, "%d_%s", i, fname); //숫자_파일명 형태
				if((fp = fopen(i_fname, "a+")) == NULL){ //fopen
					fprintf(stderr, "fopen error for %s\n", i_fname);
					exit(1);
				}
				fseek(fp, 0, SEEK_END);
				filesize = ftell(fp); //file size 구함
			}

		}

		if(i!=0){ //같은 이름의 파일이 있을 경우
			sprintf(newpath, "%s/%d_%s", filesDir, i, fname); //trash로 옮길 절대경로
		}


		stat(oldpath, &statbuf); //삭제할 파일의 stat 구조체

		if(rename(oldpath, newpath) < 0){ //trash/files로 파일 이동
			fprintf(stderr, "존재하지 않는 파일입니다.\n");
			return;
		}

		//삭제 시간 구함
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		strftime(tmp, BUFFER_SIZE, "D : %F %H:%M:%S", timeinfo);

		//info 파일 작성
		fprintf(fp, "%s\n", "[Trash info]");
		fprintf(fp, "%s\n", oldpath);
		fprintf(fp, "%s\n", tmp);
	
		memset(tmp, 0, BUFFER_SIZE);

		//수정 시간 구함
		timeinfo = localtime(&statbuf.st_mtime);
		strftime(tmp, BUFFER_SIZE, "M : %F %H:%M:%S", timeinfo);
		fprintf(fp, "%s\n", tmp); //info 파일에 작성
		fclose(fp);

		dirsize = get_infoDir_size(dirsize); //info 디렉토리 사이즈 확인
		time_t oldest = 0; //mtime 비교대상
		if(dirsize > 2000){ //info 디렉토리 크기가 2KB보다 클 경우
			while(dirsize >= 2000){ //2KB보다 작을 때 까지 파일 삭제 반복
				if((filenum = scandir(infoDir, &namelist, NULL, NULL)) == -1){
					fprintf(stderr, "scandir error\n");
					exit(1);
				}

				oldest = 0;

				for(int i=0; i<filenum; i++){ //파일 개수만큼 반복
					if(!strcmp(namelist[i]->d_name, ".") || !strcmp(namelist[i]->d_name, ".."))
						continue;

					realpath(namelist[i]->d_name, path); //절대경로 변환
					stat(path, &statbuf);
					
					if(oldest == 0){ //처음 비교 시 oldest에 값 대입
						oldest = statbuf.st_mtime;
						memset(oldpath, 0, PATH_MAX);
						strcpy(oldpath, path); //oldpath에 절대경로 저장
					}

					//가장 오래된 파일 구하기
					if(oldest > statbuf.st_mtime){
						oldest = statbuf.st_mtime;
						memset(oldpath, 0, PATH_MAX);
						strcpy(oldpath, path);
					}

				}
				
				//오래된 파일 삭제
				stat(oldpath, &statbuf);
				memset(path, 0, PATH_MAX);
				remove(oldpath); //info 파일 삭제
				ptr = strrchr(oldpath, '/'); //가장 마지막 / 위치 리턴
				ptr++; //파일명 구하기
				strcpy(path, ptr); //path에 파일명 복사
				sprintf(oldpath, "%s/%s", filesDir, path); //filesDir/파일명 절대경로 저장
				stat(oldpath, &statbuf);
				if(S_ISDIR(statbuf.st_mode)){ //삭제할 파일이 디렉토리일 때
					remove_dir(oldpath); //디렉토리 삭제 함수
				}
				remove(oldpath); //files 파일 삭제
			
				dirsize = get_infoDir_size(dirsize); //디렉토리 사이즈 구하기
			}
		}
			
		chdir(saved_path); //작업 디렉토리로 복귀

		exit(0);
	}
}

void remove_dir(char *oldpath){ //디렉토리 삭제 함수
	struct dirent **namelist;
	struct stat statbuf; //stat 구조체
	char path[PATH_MAX];
	int filenum = 0;

	chdir(oldpath);
	if((filenum = scandir(oldpath, &namelist, NULL, NULL)) == -1){
		fprintf(stderr, "scandir error\n");
		return;
	}
	
	for(int i=0; i<filenum; i++){ //file 개수만큼 반복
		if(!strcmp(namelist[i]->d_name, ".") || !strcmp(namelist[i]->d_name, ".."))
			continue;

		memset(path, 0, PATH_MAX);
		realpath(namelist[i]->d_name, path); //절대경로 변환
		stat(path, &statbuf);
		
		if(S_ISDIR(statbuf.st_mode)) //디렉토리일 경우
			remove_dir(path); //재귀함수
		else //파일이면 remove
			remove(path);
	}

	return;
}

off_t get_infoDir_size(off_t dirsize){ //infoDir 사이즈를 구하는 함수
	int filenum;
	struct dirent **namelist;
	struct stat statbuf; //stat 구조체
	char path[PATH_MAX];

	if((filenum = scandir(infoDir, &namelist, NULL, NULL)) == -1){ //file 개수 리턴
		fprintf(stderr, "scandir error\n");
		exit(1);
	}

	dirsize=0;
	for(int i=0; i<filenum; i++){ //파일 개수만큼 반복
		if(!strcmp(namelist[i]->d_name, ".") || !strcmp(namelist[i]->d_name, ".."))
			continue;

		realpath(namelist[i]->d_name, path);
		stat(path, &statbuf);
		
		dirsize += statbuf.st_size; //파일 사이즈 누적
	}

	return dirsize;
}

void do_recover(char **input_token){
	struct dirent **namelist;
	struct stat statbuf; //stat 구조체
	int filenum;
	int dupfilenum = 0;
	int dupfile[PATH_MAX] = {0};
	FILE *fp;
	char newpath[PATH_MAX];
	char oldpath[PATH_MAX];
	char newtemp[PATH_MAX];
	char tmp[4][BUFFER_SIZE];
	char filename[BUFFER_SIZE] = {0};
	char i_fname[BUFFER_SIZE] = {0};
	char *ptr;
	int i=0, j=0;
	int input;

	chdir(filesDir);
	if((filenum = scandir(filesDir, &namelist, NULL, NULL)) == -1){ //files 디렉토리 scan
		fprintf(stderr, "scandir error\n");
		exit(1);
	}

	chdir(infoDir);
	//중복 파일 검사
	j=0;
	for(i=0; i<filenum; i++){
		for(int k=0; k<4; k++)
			memset(tmp[k], 0, BUFFER_SIZE);

		if(!strcmp(namelist[i]->d_name, ".") || !strcmp(namelist[i]->d_name, ".."))
			continue;

		if((fp = fopen(namelist[i]->d_name, "r")) == NULL){ //info file open
			fprintf(stderr, "fopen error for %s\n", namelist[i]->d_name);
			return;
		}

		fscanf(fp, "%[^\n]\n", tmp[0]);
		fscanf(fp, "%[^\n]\n", tmp[1]);
		ptr = strrchr(tmp[1], '/');
		ptr++; //파일 이름 구하기

		if(!strcmp(ptr, input_token[1])){
			dupfile[j++] = i;
			dupfilenum++;
		}
	}

	if(dupfilenum == 1){ //중복 파일이 없을 때
		for(j=0; j<4; j++)
			memset(tmp[j], 0, BUFFER_SIZE);
		
		chdir(filesDir);
		realpath(namelist[dupfile[0]]->d_name, oldpath); //절대경로 변환
		chdir(infoDir);
		if((fp = fopen(namelist[dupfile[0]]->d_name, "r")) == NULL){ //info file open
			fprintf(stderr, "fopen error for %s\n", input_token[1]);
			return;
		}
		
		j=0;
		while(fscanf(fp, "%[^\n]\n", tmp[j]) != EOF){ //info file line 단위로 읽음
			j++;
		}

		strcpy(newpath, tmp[1]); //복구할 파일의 절대경로
		if(access(newpath, F_OK) == 0){ //같은 이름의 파일이 존재할 때
			i=0;
			ptr = strrchr(newpath, '/');
			ptr++; //파일명 구하기
			strcpy(filename, ptr); //파일명 저장
			memcpy(newtemp, newpath, strlen(newpath)-strlen(ptr)); //파일명 전까지 경로 저장
			while(access(newpath, F_OK) == 0){ //같은 이름의 파일이 없을 때까지 반복
				i++;
				memset(i_fname, 0, BUFFER_SIZE);
				sprintf(i_fname, "%d_%s", i, filename); //숫자_파일명 형태
				memcpy(newpath+(strlen(newtemp)), i_fname, strlen(i_fname)); //새로운 경로
			}
		}
	
		if(rename(oldpath, newpath)<0){ //파일 복구
			//복구 경로가 잘못된 경우 (ex. 디렉토리가 없을 경우)
			printf("recover path is not exists.\n");
			return;
		}
		remove(namelist[dupfile[0]]->d_name); //info 파일 지우기
	}
	else if(dupfilenum > 1){ //중복 파일이 있을 때
		chdir(infoDir);
		for(i=0; i<dupfilenum; i++){ //중복 파일 개수만큼 반복
			for(j=0; j<4; j++)
				memset(tmp[j], 0, BUFFER_SIZE);
			
			if((fp = fopen(namelist[dupfile[i]]->d_name, "r")) == NULL){ //info file open
				fprintf(stderr, "fopen error for %s\n", input_token[1]);
				return;
			}
			
			j=0;
			fseek(fp, 0, SEEK_SET);
			while(fscanf(fp, "%[^\n]\n", tmp[j]) != EOF){ //info file line 단위로 읽음
				j++;
			}

			printf("%d: %s %s %s\n", i+1, input_token[1], tmp[2], tmp[3]); //파일 정보 출력
		}
		printf("Choose : ");
		scanf("%d", &input); //복구할 파일 선택
		getchar(); //버퍼 비우기

		chdir(filesDir);
		realpath(namelist[dupfile[input-1]]->d_name, oldpath); //복구할 파일 절대경로 변환
		chdir(infoDir);
		if((fp = fopen(namelist[dupfile[input-1]]->d_name, "r")) == NULL){ //info file open
			fprintf(stderr, "fopen error for %s\n", input_token[1]);
			return;
		}
		
		j=0;
		while(fscanf(fp, "%[^\n]\n", tmp[j]) != EOF){ //info file line 단위로 읽음
			j++;
		}

		strcpy(newpath, tmp[1]); //복구할 위치 절대경로

		if(access(newpath, F_OK) == 0){ //같은 이름의 파일이 존재할 때
			i=0;
			ptr = strrchr(newpath, '/');
			ptr++; //파일명 구하기
			strcpy(filename, ptr); //파일명 저장 
			memcpy(newtemp, newpath, strlen(newpath)-strlen(ptr)); //파일명 전까지 경로 저장
	
			while(access(newpath, F_OK) == 0){ //같은 파일이 존재하지 않을 때까지 반복
				i++;
				memset(i_fname, 0, BUFFER_SIZE);
				sprintf(i_fname, "%d_%s", i, filename); //숫자_파일명 형태
				memcpy(newpath+(strlen(newtemp)), i_fname, strlen(i_fname)); //새로운 경로
			}
		}
	
		if(rename(oldpath, newpath)<0){ //파일 복구
			//복구 경로가 잘못된 경우 (ex. 디렉토리가 없을 경우)
			fprintf(stderr, "recover path is not exists.\n");
			return;
		}
		remove(namelist[dupfile[input-1]]->d_name); //info 파일 지우기

	}
	else if(dupfilenum == 0) //파일이 없을 때
		printf("There is no '%s' in the 'trash' directory\n", input_token[1]);
	
	return;

}

void print_tree(char dirpath[], int level, int p_end){
	int filenum;
	int i,j;
	int cnt=0;
	int is_end = 0;
	char *dirname;
	char path[PATH_MAX];
	struct dirent **namelist;
	struct stat statbuf;

	chdir(dirpath); //해당 디렉토리로 이동

	if((filenum = scandir(".", &namelist, NULL, alphasort)) == -1){ //dir scan
		fprintf(stderr, "scandir error\n");
		return;
	}

	for(i=0; i<filenum; i++){ //파일 개수만큼 반복
		if(i==(filenum)-1) //마지막 파일이면 is_end == 1
			is_end = 1;

		memset(path, 0, PATH_MAX);
		if(!strcmp(namelist[i]->d_name, ".") || !strcmp(namelist[i]->d_name, ".."))
			continue;

		realpath(namelist[i]->d_name, path); //절대경로로 변경
		stat(path, &statbuf); //stat 구조체 선언

		if(cnt==0) //첫 번째 파일일 때
			printf("----------%s", namelist[i]->d_name);
		else{
			if(level>1 && p_end){ //레벨 1 이상이고 디렉토리가 마지막 파일일 때
				for(j=0; j<level; j++)
					printf("%15c", ' '); //중간 | 출력하지 않음

				printf("|");
			}
			else{
				for(j=0; j<level; j++)
					printf("%15c", '|'); //중간 | 출력
			}
			printf("\n");
			
			for(j=0; j<level; j++)
				printf("%15c", ' '); //레벨만큼 공백 출력

			printf("--%s", namelist[i]->d_name); //파일 이름 출력
		}

		if(S_ISDIR(statbuf.st_mode)){ //디렉토리일 때
			print_tree(path, level+1, is_end); //level+1과 마지막 파일 정보 주고 재귀함수
			chdir(dirpath); //다시 원래 디렉토리로 돌아옴
		}
		else
			printf("\n"); //디렉토리가 아니면 개행
		
		cnt++;
	}

	return;
}

void ssu_runtime(){ //시간 계산 함수
	end_t.tv_sec -= begin_t.tv_sec;

	if(end_t.tv_usec < begin_t.tv_usec){
		end_t.tv_sec--;
		end_t.tv_usec += 1000000;
	}

	end_t.tv_usec -= begin_t.tv_usec;
	printf("Runtime: %ld:%06ld(sec:usec)\n", end_t.tv_sec, end_t.tv_usec);
}

int daemon_init(void){
	FILE *fp;
	pid_t pid;
	int fd, maxfd;

	if((pid = fork()) < 0){ //자식 프로세스 생성
		fprintf(stderr, "fork error\n"); //에러 처리
		exit(1);
	}
	else if(pid != 0) //부모 프로세스 종료
		exit(0);

	pid = getpid();
	setsid(); //디몬 프로세스 생성
	//터미널 입출력 시그널 무시
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	maxfd = getdtablesize(); //프로세스가 가질 수 있는 최대 파일 개수

	for(fd = 0; fd < maxfd; fd++)
		close(fd); //모든 파일 디스크립터 close

	umask(0);
	chdir("/");
	fd = open("/dev/null", O_RDWR);
	dup(0);
	dup(0);
	
	while(1){
		ssu_mntr(checkDir); //로그 기록
	}
}
