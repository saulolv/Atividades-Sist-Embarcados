#ifndef THREADS_H
#define THREADS_H

void sensor_thread_entry(void *p1, void *p2, void *p3);
void display_thread_entry(void *p1, void *p2, void *p3);
void camera_thread_entry(void *p1, void *p2, void *p3);

#endif

