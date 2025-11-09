#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

int x[8] = {1,0,-1,0,1,-1,1,-1};
int y[8] = {0,1,0,-1,-1,1,1,-1};

int mode;
int parallel_length;
int parallel_width;
int parallel_generations;
int thread_count;
char **parallel_a;
char **parallel_next_a;
pthread_barrier_t my_barrier;

char** alloc_matrix(int length, int width){
    char **a = malloc(length * sizeof(char*));
    if (!a){
        perror("couldnt allocate mem for matrix");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < length; ++i){
        a[i] = malloc(width * sizeof(char));
        if (!a[i]){
            perror("couldnt allocate mem for matrix");
            for (int j = i - 1; j >= 0; --j)
                free(a[j]);
            free(a);
            exit(EXIT_FAILURE);
        }
    }
    return a;
}

void free_matrix(char **a, int length){
    if (a == NULL){
        return;
    }
    for (int i = 0; i < length; ++i){
        free(a[i]);
    }
    free(a);
}

char** init(int *length, int *width, FILE* input){
    fscanf(input,"%d %d", &(*length), &(*width));
    parallel_length = *length;
    parallel_width = *width;
    char **a = alloc_matrix(*length, *width);
    for( int i = 0 ; i < *length; ++i ){
        for(int j = 0 ; j < *width; ++j ){
            char ch = fgetc(input);
            while (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t'){
                ch = fgetc(input);
            }
            a[i][j] = ch;
        }
    }
    return a;
}

void show_matrix(char**a , int length, int width){
    for(int i = 0 ; i < length; ++i ){
        for(int j = 0 ; j < width; ++j ){
            printf("%c", a[i][j]);
            
        }
        printf("\n");
    }
    printf("\n\n");
}

void show_matrix_in_file(char**a , int length, int width,FILE* file){
    for(int i = 0 ; i < length; ++i ){
        for(int j = 0 ; j < width; ++j ){
            fprintf(file,"%c", a[i][j]);
            
        }
        fprintf(file,"\n");
    }
}

int check_serial_vs_parallel(char**a_serial, char **a_parallel, int length, int width){
    for(int i = 0 ; i < length ; ++i){
        for(int j = 0 ; j < width; ++j ){
            if(a_serial[i][j] != a_parallel[i][j]) return 0;
        }
    }
    return 1;
}

void copy_matrix(char**dst, char**src, int length, int width){
    for(int i = 0 ; i < length; ++i ){
        for(int j = 0 ; j < width; ++j ){
            dst[i][j] = src[i][j];
        }
    }
}

void copy_matrix_parallel(char**dst, char**src, int start_index,int end_index, int width){
    for(int i = start_index ; i < end_index; ++i ){
        for(int j = 0 ; j < width; ++j ){
            dst[i][j] = src[i][j];
        }
    }
}


void generations_serial(char**a, char** next_a, int generations, int length, int width){
    for( int g = 0 ; g < generations; ++g ){
        for(int i = 0 ; i < length; ++i ){
            for(int j = 0; j < width; ++j ){
                int neighbours = 0;
                for(int k = 0; k < 8; ++k ){
                    if(i + y[k] < 0 || i + y[k] >= length || j + x[k] < 0 || j + x[k] >= width) continue;
                    if(a[i+y[k]][j+x[k]] == 'X') neighbours++;
                }
                if( a[i][j] == '.' && neighbours == 3 ) next_a[i][j] = 'X';
                else if(a[i][j] == 'X' && neighbours < 2 || neighbours > 3) next_a[i][j] = '.';
                else next_a[i][j] = a[i][j];
            }
        }
        copy_matrix(a,next_a,length,width);
        if( mode == 1 ){
            printf("Generation serial %d: \n", g);
            show_matrix(a,length,width);
        }
    }
    //printf("The colony after %d serial generations looks like this: \n\n", generations);
    
}

void* generations_parallel_worker(void* rank){
    int my_rank = *(int*)rank;
    int rows_per_thread = parallel_length/thread_count;
    int remainder_rows = parallel_length%thread_count;
    int actual_rows = rows_per_thread;
    if(my_rank < remainder_rows)
        actual_rows += 1;
    int start_index = my_rank * rows_per_thread;
    if( my_rank < remainder_rows){
        start_index += my_rank;
    }
    else start_index += remainder_rows;
    int end_index = start_index + actual_rows;
    for(int g = 0 ; g < parallel_generations; ++g){
        for(int i = start_index ; i < end_index; ++i ){
            for(int j = 0; j < parallel_width; ++j ){
                int neighbours = 0;
                for(int k = 0; k < 8; ++k ){
                    if(i + y[k] < 0 || i + y[k] >= parallel_length || j + x[k] < 0 || j + x[k] >= parallel_width) continue;
                    if(parallel_a[i+y[k]][j+x[k]] == 'X') neighbours++;
                }
                if( parallel_a[i][j] == '.' && neighbours == 3 ) parallel_next_a[i][j] = 'X';
                else if(parallel_a[i][j] == 'X' && neighbours < 2 || neighbours > 3) parallel_next_a[i][j] = '.';
                else parallel_next_a[i][j] = parallel_a[i][j];
            }
            
        }
        pthread_barrier_wait(&my_barrier);
        copy_matrix_parallel(parallel_a,parallel_next_a,start_index,end_index,parallel_width);
        pthread_barrier_wait(&my_barrier);
        if(mode == 1 && my_rank == 0){
            printf("Generation parallel %d: \n",g);
            show_matrix(parallel_a,parallel_length,parallel_width);
        }
        pthread_barrier_wait(&my_barrier);
    }
    return NULL;
}

void generations_parallel(char **a, int generations, int length, int width){
    int *tid = malloc(thread_count*sizeof(int));
    if(tid == NULL ){
        perror("couldnt allocate memory");
        exit(EXIT_FAILURE);
    }
    pthread_t *threads = malloc(thread_count*sizeof(pthread_t));
    if(threads == NULL ){
        perror("couldnt allocate memory");
        exit(EXIT_FAILURE);
    }
    pthread_barrier_init(&my_barrier,NULL,thread_count);
    for(int i = 0; i < thread_count; ++i ){
        tid[i] = i;
        pthread_create(&threads[i], NULL, generations_parallel_worker, &tid[i]);
    }
    for(int i = 0 ; i < thread_count; ++i ){
        pthread_join(threads[i],NULL);
    }
    pthread_barrier_destroy(&my_barrier);
    //printf("The colony after %d parallel generations looks like this:\n\n",parallel_generations);
    
}

int main(int argc, char** argv){
    if( argc != 4 ){    
        printf("Usage: ./prog <input_file> <no_of_generations> <no_of_threads>");
        exit(EXIT_FAILURE);
    }
    FILE *f_input = fopen(argv[1], "r");
    if( f_input == NULL ){
        perror("couldn't open input file: ");
        exit(EXIT_FAILURE);
    }
    FILE *f_serial = fopen("f_serial_out.txt", "w");
    if( f_serial == NULL ){
        perror("couldn't open output serial file: ");
        exit(EXIT_FAILURE);
    }
    FILE *f_parallel = fopen("f_parallel_out.txt", "w");
    if( f_parallel == NULL ){
        perror("couldn't open output serial file: ");
        exit(EXIT_FAILURE);
    }
    printf("Please choose the mode you want to run in:\n\n1.DEBUG MODE\n2.NORMAL MODE\n\n");
    scanf("%d", &mode);
    int length, width;
    int generations = atoi(argv[2]);
    parallel_generations = generations;
    int n_threads = atoi(argv[3]);
    thread_count = n_threads;
    char **a_serial = NULL;
    a_serial = init(&length,&width,f_input);
    char **next_a_serial = alloc_matrix(length,width);
    parallel_a = alloc_matrix(length,width);
    parallel_next_a = alloc_matrix(length,width);
    copy_matrix(parallel_a, a_serial,length,width);
    generations_serial(a_serial,next_a_serial,generations,length,width);
    show_matrix_in_file(a_serial,length,width,f_serial);
    generations_parallel(parallel_a,generations,length,width);
    show_matrix_in_file(parallel_a,length,width,f_parallel);

    

    if(check_serial_vs_parallel(a_serial,parallel_a,length,width)) printf("Parallel and serial output are the same");
    else printf("Parallel and serial output is NOT the same");
    free_matrix(parallel_a, length);
    free_matrix(a_serial, length);
    free_matrix(next_a_serial,length);
    free_matrix(parallel_next_a,length);
    fclose(f_input);
    fclose(f_serial);
    fclose(f_parallel);
    return 0;
}
