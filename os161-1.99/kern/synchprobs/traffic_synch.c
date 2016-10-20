#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

/*
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/*
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to
 * declare other global variables if your solution requires them.
 */

#define MAX_CARS 10


/*
 * store cars in the intersection
 */
volatile int cars_crossing[4][4] = { {0} };

/*
 * Counter for cars in the intersection
 */
volatile int car_count = 0;

/*
 * Declarations of some helper functions
 */
void add_car(Direction origin, Direction destination);
bool is_right_turn(Direction origin, Direction destination);
bool check_intersection (Direction origin, Direction destination);




/*
 * The synchronization primitives, a cv and the lock that works with the cv
 */
static struct cv *intersectionCv;
static struct lock *intersectionLock;
static struct lock *exitLock;


/*
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 *
 */
void
intersection_sync_init(void)
{
        intersectionCv = cv_create("intersectionCv");
        if (intersectionCv == NULL) {
                panic("could not create intersection cv");
        }
        
        intersectionLock = lock_create("intersectionLock");
        if (intersectionLock == NULL) {
                panic("could not create intersection lock");
        }
        
        exitLock = lock_create("exitLock");
        if (exitLock == NULL) {
                panic("could not create exit lock");
        }
        
        return;
}

/*
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
        KASSERT(intersectionLock != NULL);
        KASSERT(intersectionCv != NULL);
        KASSERT(exitLock != NULL);
        
        cv_destroy(intersectionCv);
        lock_destroy(intersectionLock);
        lock_destroy(exitLock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination)
{
        KASSERT(intersectionLock != NULL);
        KASSERT(intersectionCv != NULL);
        
        lock_acquire(intersectionLock);
        while (car_count >= MAX_CARS || !check_intersection(origin, destination)) {
                cv_wait(intersectionCv, intersectionLock);
        }
        add_car(origin, destination);
        lock_release(intersectionLock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination)
{
        KASSERT(intersectionLock != NULL);
        KASSERT(intersectionCv != NULL);
        
        lock_acquire(exitLock);
        cars_crossing[origin][destination]--;
        car_count--;
        cv_signal(intersectionCv, intersectionLock);
        lock_release(exitLock);
        
        
}

/*
 * Helper function that accepts two directions as parameters and check if this vehicle is good to enter
 * the intersection.
 */

bool
check_intersection (Direction origin, Direction destination)
{
        for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                        if (cars_crossing[i][j] != 0) {
                                Direction o = (Direction)i;
                                Direction d = (Direction)j;
                                if (!(o == origin
                                    || (o == destination && d == origin)
                                    || (d != destination
                                        && (is_right_turn(origin, destination)
                                            || is_right_turn(o, d)))))
                                        return false;
                        }
                }
        }
        return true;
}

/*
 * Helper function that takes two directions of a car and check if this car is making right turn.
 */

bool
is_right_turn(Direction origin, Direction destination) {
        return (origin == south && destination == east)
            || (origin == north && destination == west)
            || (origin == east && destination == north)
            || (origin == west && destination == south);
}

/*
 * These two functions can only be called in a critical section.
 */


/*
 * Helper function that adds a new car to the car_crossing array. Increment car_count by 1;
 */

void
add_car(Direction origin, Direction destination)
{
        KASSERT(car_count <= MAX_CARS);
        
        cars_crossing[(int)origin][(int)destination]++;
        car_count++;
        
}



















