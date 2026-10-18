#include <stdio.h>

int main(){
    FILE * fp = fopen("haikus.txt","r");
    char buffer[256];
    int choice = -1;
    if(fp == NULL){
        perror("fopen");
        return 1;
    }

    while(1){
        printf("Do you want a haiku? 1 - yes, 0 - no");
        scanf("%d", &choice);

        if(!choice) break;
        else{
            while(fgets(buffer,sizeof(buffer),fp) != NULL){
                if(buffer[0] == '\n') break;
                printf("%s", buffer);
            }
            if(feof(fp)) break;
        }
    }
}