#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <sys/wait.h>

#define MAX_DRIVERS 16
#define BUF 128

typedef struct {
    pid_t pid;
    int to_driver;
    int from_driver;
} Driver;

Driver drivers[MAX_DRIVERS];
int count = 0;

void driver_loop(int rfd, int wfd)
{
    char buf[BUF];
    int busy = 0;

    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);

    while (1)
    {
        struct pollfd fds[2] = {
            { rfd, POLLIN, 0 },
            { tfd, POLLIN, 0 }
        };

        poll(fds, 2, -1);

        if (fds[0].revents & POLLIN)
        {
            int n = read(rfd, buf, BUF - 1);
            if (n <= 0)
                exit(0);
            buf[n] = '\0';

            if (!strncmp(buf, "TASK", 4))
            {
                int sec = atoi(buf + 5);
                if (busy)
                {
                    write(wfd, "BUSY\n", 5);
                }
                else
                {
                    struct itimerspec ts = {0};
                    ts.it_value.tv_sec = sec;
                    timerfd_settime(tfd, 0, &ts, NULL);
                    busy = 1;
                    write(wfd, "OK\n", 3);
                }
            }

            else if (!strncmp(buf, "STATUS", 6))
            {
                if (busy)
                    write(wfd, "BUSY\n", 5);
                else
                    write(wfd, "AVAILABLE\n", 10);
            }
        }

        if (fds[1].revents & POLLIN)
        {
            unsigned long long x;
            read(tfd, &x, sizeof(x));
            busy = 0;
        }
    }
}

Driver* find(pid_t pid)
{
    for (int i = 0; i < count; i++)
        if (drivers[i].pid == pid)
            return &drivers[i];
    return NULL;
}

void create_driver()
{
    if (count >= MAX_DRIVERS)
    {
        printf("Max drivers reached\n");
        return;
    }

    int to_d[2], from_d[2];
    pipe(to_d);
    pipe(from_d);

    pid_t pid = fork();
    if (pid == 0)
    {
        close(to_d[1]);
        close(from_d[0]);
        driver_loop(to_d[0], from_d[1]);
        exit(0);
    }

    close(to_d[0]);
    close(from_d[1]);

    drivers[count++] = (Driver){ pid, to_d[1], from_d[0] };
    printf("Driver created. PID = %d\n", pid);
}

void send_task()
{
    pid_t pid;
    int sec;

    printf("Enter driver PID: ");
    scanf("%d", &pid);

    printf("Enter task time (seconds): ");
    scanf("%d", &sec);

    Driver* d = find(pid);
    if (!d)
    {
        printf("No such driver\n");
        return;
    }

    dprintf(d->to_driver, "TASK %d\n", sec);

    char buf[BUF];
    int n = read(d->from_driver, buf, BUF - 1);
    buf[n] = '\0';
    printf("Driver response: %s", buf);
}

void get_status()
{
    pid_t pid;
    printf("Enter driver PID: ");
    scanf("%d", &pid);

    Driver* d = find(pid);
    if (!d)
    {
        printf("No such driver\n");
        return;
    }

    dprintf(d->to_driver, "STATUS\n");

    char buf[BUF];
    int n = read(d->from_driver, buf, BUF - 1);
    buf[n] = '\0';
    printf("PID %d: %s", pid, buf);
}

void get_drivers()
{
    if (count == 0)
    {
        printf("No drivers created\n");
        return;
    }

    for (int i = 0; i < count; i++)
    {
        dprintf(drivers[i].to_driver, "STATUS\n");
        char buf[BUF];
        int n = read(drivers[i].from_driver, buf, BUF - 1);
        buf[n] = '\0';
        printf("PID %d: %s", drivers[i].pid, buf);
    }
}

void print_menu()
{
    printf("\nMenu:\n");
    printf("1 - Create driver\n");
    printf("2 - Send task\n");
    printf("3 - Get driver status\n");
    printf("4 - Get all drivers\n");
    printf("0 - Exit\n");
    printf("Enter number: ");
}

int main()
{
    int choice;

    while (1)
    {
        print_menu();
        if (scanf("%d", &choice) != 1)
            break;

        switch (choice)
        {
            case 1:
                create_driver();
                break;
            case 2:
                send_task();
                break;
            case 3:
                get_status();
                break;
            case 4:
                get_drivers();
                break;
            case 0:
                goto exit;
            default:
                printf("Unknown option\n");
        }
    }

exit:
    for (int i = 0; i < count; i++)
        kill(drivers[i].pid, SIGTERM);
    while (wait(NULL) > 0);

    return 0;
}