#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <time.h>
#include <sys/shm.h>

float **read_matrix(const char *filepath, int *row_count, int *col_count)
{
    FILE *fp = fopen(filepath, "r");
    if (!fp)
    {
        perror("File open failed");
        return NULL;
    }

    float **mat = NULL;
    *row_count = 0;
    *col_count = 0;

    char *buffer = NULL;
    size_t buf_len = 0;
    ssize_t line_size;

    while ((line_size = getline(&buffer, &buf_len, fp)) != -1)
    {
        char *part;
        int col_idx = 0;

        mat = realloc(mat, (*row_count + 1) * sizeof(float *));
        if (!mat)
        {
            perror("realloc failed");
            fclose(fp);
            free(buffer);
            return NULL;
        }

        mat[*row_count] = NULL;

        part = strtok(buffer, " \n");
        while (part)
        {
            mat[*row_count] = realloc(mat[*row_count], (col_idx + 1) * sizeof(float));
            if (!mat[*row_count])
            {
                perror("inner realloc failed");
                fclose(fp);
                free(buffer);
                return NULL;
            }
            mat[*row_count][col_idx] = atof(part);
            part = strtok(NULL, " \n");
            col_idx++;
        }

        if (*col_count == 0)
        {
            *col_count = col_idx;
        }
        else if (col_idx != *col_count)
        {
            fprintf(stderr, "Column mismatch at row %d\n", *row_count);
            fclose(fp);
            free(buffer);
            return NULL;
        }

        (*row_count)++;
    }

    free(buffer);
    fclose(fp);
    return mat;
}



float *matrix_multiply_parallel(float **mat_a, float **mat_b, int num_workers,
                                int a_rows, int a_cols, int b_rows, int b_cols)
{
    if (a_cols != b_rows)
    {
        fprintf(stderr, "Matrix size incompatible for multiplication\n");
        return NULL;
    }

    int rows_per_worker = a_rows / num_workers;
    int leftover = a_rows % num_workers;

    int shared_id = shmget(IPC_PRIVATE, sizeof(float) * a_rows * b_cols, IPC_CREAT | 0666);
    if (shared_id < 0)
    {
        perror("shmget error");
        return NULL;
    }

    float *shared_result = (float *)shmat(shared_id, NULL, 0);
    if (shared_result == (float *)-1)
    {
        perror("shmat error");
        shmctl(shared_id, IPC_RMID, NULL);
        return NULL;
    }

    for (int w = 0; w < num_workers; w++)
    {
        int begin = w * rows_per_worker + (w < leftover ? w : leftover);
        int current_rows = rows_per_worker + (w < leftover ? 1 : 0);

        pid_t child_pid = fork();

        if (child_pid < 0)
        {
            perror("fork error");
            shmdt(shared_result);
            shmctl(shared_id, IPC_RMID, NULL);
            exit(1);
        }
        else if (child_pid == 0)
        {
            for (int r = begin; r < begin + current_rows; r++)
            {
                for (int c = 0; c < b_cols; c++)
                {
                    shared_result[r * b_cols + c] = 0;
                    for (int i = 0; i < a_cols; i++)
                    {
                        shared_result[r * b_cols + c] += mat_a[r][i] * mat_b[i][c];
                    }
                }
            }

            shmdt(shared_result);
            exit(0);
        }
    }

    for (int i = 0; i < num_workers; i++)
        wait(NULL);


    return shared_result;
}


void release_matrix(float **mat, int row_total)
{
    for (int r = 0; r < row_total; r++)
        free(mat[r]);
    free(mat);
}

float *matrix_multiply_serial(float **mat_a, float **mat_b,
                              int a_rows, int a_cols, int b_rows, int b_cols)
{
    if (a_cols != b_rows)
    {
        fprintf(stderr, "Matrix size mismatch\n");
        return NULL;
    }

    float *result_array = malloc(sizeof(float) * a_rows * b_cols);
    if (!result_array)
    {
        perror("malloc failed");
        return NULL;
    }

    for (int r = 0; r < a_rows; r++)
    {
        for (int c = 0; c < b_cols; c++)
        {
            result_array[r * b_cols + c] = 0;
            for (int i = 0; i < a_cols; i++)
            {
                result_array[r * b_cols + c] += mat_a[r][i] * mat_b[i][c];
            }
        }
    }
    return result_array;
}

void save_matrix(const char *filename, float *matrix, int rows, int cols)
{
    FILE *file = fopen(filename, "w");
    if (!file)
    {
        perror("Couldn't open file to write");
        return;
    }

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            fprintf(file, "%.2f ", matrix[r * cols + c]);
        }
        fprintf(file, "\n");
    }

    fclose(file);
}



int main()
{
    clock_t start_seq, end_seq, start_par, end_par;
    int m1_rows, m1_cols;
    int m2_rows, m2_cols;
    double time_seq, time_par;

    float **mat1 = read_matrix("A.txt", &m1_rows, &m1_cols);
    if (!mat1)
    {
        fprintf(stderr, "Could not load first matrix.\n");
        return 1;
    }

    float **mat2 = read_matrix("B.txt", &m2_rows, &m2_cols);
    if (!mat2)
    {
        fprintf(stderr, "Could not load second matrix.\n");
        release_matrix(mat1, m1_rows);
        return 1;
    }

    if (m1_cols != m2_rows)
    {
        fprintf(stderr, "Mismatch: %d cols vs %d rows.\n", m1_cols, m2_rows);
        release_matrix(mat1, m1_rows);
        release_matrix(mat2, m2_rows);
        return 1;
    }

    start_seq = clock();
    float *serial_output = matrix_multiply_serial(mat1, mat2, m1_rows, m1_cols, m2_rows, m2_cols);
    end_seq = clock();

    if (!serial_output)
    {
        fprintf(stderr, "Serial multiplication failed.\n");
        release_matrix(mat1, m1_rows);
        release_matrix(mat2, m2_rows);
        return 1;
    }

    time_seq = (double)(end_seq - start_seq) / CLOCKS_PER_SEC;
    free(serial_output);

    int worker_count;
    printf("Type in the number of processes you want to use for the application (greater than 1): ");
    if (scanf("%d", &worker_count) == 1 && worker_count > 0)
    {
        start_par = clock();
        float *parallel_output = matrix_multiply_parallel(mat1, mat2, worker_count, m1_rows, m1_cols, m2_rows, m2_cols);
        end_par = clock();
        save_matrix("C.txt", parallel_output, m1_rows, m2_cols);

        if (parallel_output)
        {
            time_par = (double)(end_par - start_par) / CLOCKS_PER_SEC;

            shmdt(parallel_output);
            int sid = shmget(IPC_PRIVATE, 0, 0666);
            shmctl(sid, IPC_RMID, NULL);


        }
        else
        {
            fprintf(stderr, "Parallel multiplication failed\n");
            release_matrix(mat1, m1_rows);
            release_matrix(mat2, m2_rows);
            return 1;
        }

        release_matrix(mat1, m1_rows);
        release_matrix(mat2, m2_rows);

        printf("Sequential time: %.10f seconds\n", time_seq);
        printf("Parallel time (%d processes): %.10f seconds\n", worker_count, time_par);
        printf("Speedup: %.6fX\n", (time_seq / time_par));
        return 0;
    }
    else
    {
        printf("Invalid input for number of processes\n");
        release_matrix(mat1, m1_rows);
        release_matrix(mat2, m2_rows);
        return 1;
    }
}
