#include "../header/functions.h"
#include <pthread.h>

char mode_start;

pthread_t threadVideoID;
pthread_t threadPostionID;

Camera rpiCam;
Image imgVideo;
Arene monArene;
Position positionRobots[20];
Jpg compress;

static void threadVideoCleanUp(void * arg) {
    if (pthread_kill(threadPostionID, 0) == 0) {
        pthread_cancel(threadPostionID);
    }
    close_camera(&rpiCam);
}

static void *threadVideo(void *arg) {
    pthread_cleanup_push(threadVideoCleanUp, NULL);
    open_camera(&rpiCam);
    while (1) {
        get_image(&rpiCam, &imgVideo);
        compress_image(&imgVideo, &compress);
        send_message_to_monitor(HEADER_STM_IMAGE, &compress);
    }
    pthread_cleanup_pop(NULL);
}

static void *threadComputePosition(void *arg) {
    while (1) {
        if (detect_arena(&imgVideo, &monArene) == 0) {
            detect_position(&imgVideo, positionRobots, &monArene);
        } else {
            detect_position(&imgVideo, positionRobots);
        }
        draw_position(&imgVideo, &imgVideo, &positionRobots[0]);
        send_message_to_monitor(HEADER_STM_POS, &positionRobots);
    }
}

void write_in_queue(RT_QUEUE *, MessageToMon);

void send_ack() {
    MessageToMon msg_ack;
    set_msgToMon_header(&msg_ack, HEADER_STM_ACK);
    write_in_queue(&q_messageToMon, msg_ack);
}

void f_server(void *arg) {
    int err;
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    err = run_nodejs("/usr/local/bin/node", "/home/pi/Interface_Robot/server.js");

    if (err < 0) {
        printf("Failed to start nodejs: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    } else {
#ifdef _WITH_TRACE_
        printf("%s: nodejs started\n", info.name);
#endif
        open_server();
        rt_sem_broadcast(&sem_serverOk);
    }
}

void f_sendToMon(void *arg) {
    int err;
    MessageToMon msg;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

#ifdef _WITH_TRACE_
    printf("%s : waiting for sem_serverOk\n", info.name);
#endif
    rt_sem_p(&sem_serverOk, TM_INFINITE);
    while (1) {

#ifdef _WITH_TRACE_
        printf("%s : waiting for a message in queue\n", info.name);
#endif
        if (rt_queue_read(&q_messageToMon, &msg, sizeof(MessageToRobot), TM_INFINITE) >= 0) {
#ifdef _WITH_TRACE_
            printf("%s : message {%s,%s} in queue\n", info.name, msg.header, msg.data);
#endif

            send_message_to_monitor(msg.header, msg.data);
            free_msgToMon_data(&msg);
            rt_queue_free(&q_messageToMon, &msg);
        } else {
            printf("Error msg queue write: %s\n", strerror(-err));
        }
    }
}

void f_receiveFromMon(void *arg) {
    MessageFromMon msg;
    int err;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

#ifdef _WITH_TRACE_
    printf("%s : waiting for sem_serverOk\n", info.name);
#endif
    rt_sem_p(&sem_serverOk, TM_INFINITE);
    do {
#ifdef _WITH_TRACE_
        printf("%s : waiting for a message from monitor\n", info.name);
#endif
        err = receive_message_from_monitor(msg.header, msg.data);
#ifdef _WITH_TRACE_
        printf("%s: msg {header:%s,data=%s} received from UI\n", info.name, msg.header, msg.data);
#endif
        if (strcmp(msg.header, HEADER_MTS_COM_DMB) == 0) {
            if (msg.data[0] == OPEN_COM_DMB) { // Open communication supervisor-robot
#ifdef _WITH_TRACE_
                printf("%s: message open Xbee communication\n", info.name);
#endif
                rt_sem_v(&sem_openComRobot);
            }
        } else if (strcmp(msg.header, HEADER_MTS_DMB_ORDER) == 0) {
            if (msg.data[0] == DMB_START_WITHOUT_WD) { // Start robot
#ifdef _WITH_TRACE_
                printf("%s: message start robot\n", info.name);
#endif
                rt_sem_v(&sem_startRobot);

            } else if ((msg.data[0] == DMB_GO_BACK)
                       || (msg.data[0] == DMB_GO_FORWARD)
                       || (msg.data[0] == DMB_GO_LEFT)
                       || (msg.data[0] == DMB_GO_RIGHT)
                       || (msg.data[0] == DMB_STOP_MOVE)) {

                rt_mutex_acquire(&mutex_move, TM_INFINITE);
                move = msg.data[0];
                rt_mutex_release(&mutex_move);
#ifdef _WITH_TRACE_
                printf("%s: message update movement with %c\n", info.name, move);
#endif

            }
        } else if (strcmp(msg.header, HEADER_MTS_CAMERA) == 0) {
            if ((msg.data[0] == CAM_OPEN)) {
                pthread_create(&threadVideoID, NULL, threadVideo, NULL);
                send_ack();

#ifdef _WITH_TRACE_
                printf("%s: message open camera with %c\n", info.name, msg.data[0]);
#endif
            } else if ((msg.data[0] == CAM_CLOSE)) {
                pthread_cancel(threadVideoID);
                send_ack();

#ifdef _WITH_TRACE_
                printf("%s: message close camera with %c\n", info.name, msg.data[0]);
#endif
            } else if ((msg.data[0] == CAM_ASK_ARENA)) {
                detect_arena(&imgVideo, &monArene);
                send_ack();

#ifdef _WITH_TRACE_
                printf("%s: message ask arena camera with %c\n", info.name, msg.data[0]);
#endif
            } else if ((msg.data[0] == CAM_ARENA_CONFIRM)) {
                draw_arena(&imgVideo, &imgVideo, &monArene);
                send_ack();
#ifdef _WITH_TRACE_
                printf("%s: message arena confirm camera with %c\n", info.name, msg.data[0]);
#endif
            } else if ((msg.data[0] == CAM_ARENA_INFIRM)) {
                Arene *p = &monArene;
                p = NULL;
                send_ack();
#ifdef _WITH_TRACE_
                printf("%s: message arena infirm camera with %c\n", info.name, msg.data[0]);
#endif
            } else if ((msg.data[0] == CAM_COMPUTE_POSITION)) {
                pthread_create(&threadPostionID, NULL, threadComputePosition, NULL);
                send_ack();
#ifdef _WITH_TRACE_
                printf("%s: message compute position camera with %c\n", info.name, msg.data[0]);
#endif
            } else if ((msg.data[0] == CAM_STOP_COMPUTE_POSITION)) {
                pthread_cancel(threadPostionID);
                send_ack();
#ifdef _WITH_TRACE_
                printf("%s: message stop compute position camera with %c\n", info.name, msg.data[0]);
#endif
            }
        }
    } while (err > 0);

}

void f_openComRobot(void *arg) {
    int err;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    while (1) {
#ifdef _WITH_TRACE_
        printf("%s : Wait sem_openComRobot\n", info.name);
#endif
        rt_sem_p(&sem_openComRobot, TM_INFINITE);
#ifdef _WITH_TRACE_
        printf("%s : sem_openComRobot arrived => open communication robot\n", info.name);
#endif
        err = open_communication_robot();
        if (err == 0) {
#ifdef _WITH_TRACE_
            printf("%s : the communication is opened\n", info.name);
#endif
            MessageToMon msg;
            set_msgToMon_header(&msg, HEADER_STM_ACK);
            write_in_queue(&q_messageToMon, msg);
        } else {
            MessageToMon msg;
            set_msgToMon_header(&msg, HEADER_STM_NO_ACK);
            write_in_queue(&q_messageToMon, msg);
        }
    }
}

void f_startRobot(void *arg) {
    int err;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    while (1) {
#ifdef _WITH_TRACE_
        printf("%s : Wait sem_startRobot\n", info.name);
#endif
        rt_sem_p(&sem_startRobot, TM_INFINITE);
#ifdef _WITH_TRACE_
        printf("%s : sem_startRobot arrived => Start robot\n", info.name);
#endif
        err = send_command_to_robot(DMB_START_WITHOUT_WD);
        if (err == 0) {
#ifdef _WITH_TRACE_
            printf("%s : the robot is started\n", info.name);
#endif
            rt_mutex_acquire(&mutex_robotStarted, TM_INFINITE);
            robotStarted = 1;
            rt_mutex_release(&mutex_robotStarted);
            MessageToMon msg;
            set_msgToMon_header(&msg, HEADER_STM_ACK);
            write_in_queue(&q_messageToMon, msg);
        } else {
            MessageToMon msg;
            set_msgToMon_header(&msg, HEADER_STM_NO_ACK);
            write_in_queue(&q_messageToMon, msg);
        }
    }
}

void f_move(void *arg) {
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    /* PERIODIC START */
#ifdef _WITH_TRACE_
    printf("%s: start period\n", info.name);
#endif
    rt_task_set_periodic(NULL, TM_NOW, 100000000);
    while (1) {
#ifdef _WITH_TRACE_
        printf("%s: Wait period \n", info.name);
#endif
        rt_task_wait_period(NULL);
#ifdef _WITH_TRACE_
        printf("%s: Periodic activation\n", info.name);
        printf("%s: move equals %c\n", info.name, move);
#endif
        rt_mutex_acquire(&mutex_robotStarted, TM_INFINITE);
        if (robotStarted) {
            rt_mutex_acquire(&mutex_move, TM_INFINITE);
            send_command_to_robot(move);
            rt_mutex_release(&mutex_move);
#ifdef _WITH_TRACE_
            printf("%s: the movement %c was sent\n", info.name, move);
#endif
        }
        rt_mutex_release(&mutex_robotStarted);
    }
}

void write_in_queue(RT_QUEUE *queue, MessageToMon msg) {
    void *buff;
    buff = rt_queue_alloc(&q_messageToMon, sizeof(MessageToMon));
    memcpy(buff, &msg, sizeof(MessageToMon));
    rt_queue_send(&q_messageToMon, buff, sizeof(MessageToMon), Q_NORMAL);
}