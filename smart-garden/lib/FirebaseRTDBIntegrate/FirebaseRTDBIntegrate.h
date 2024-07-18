#ifndef FIREBASE_RTDB_INTEGRATE_H
#define FIREBASE_RTDB_INTEGRATE_H

#include <Arduino.h>
#include <FirebaseClient.h>

typedef struct
{
    AsyncClientClass *client;
    RealtimeDatabase *rtdb;
    String prefixPath;

} fbrtdb_object;

#endif // FIREBASE_RTDB_INTEGRATE_H