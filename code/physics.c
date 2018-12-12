/**
 * This is the main file of the ESPLaboratory Demo project.
 * It implements simple sample functions for the usage of UART,
 * writing to the display and processing user inputs.
 *
 * @author: Jonathan Müller-Boruttau,
 * 			Tobias Fuchs tobias.fuchs@tum.de
 * 			Nadja Peters nadja.peters@tum.de (RCS, TUM)
 *
 */
#include "includes.h"

#define LONG_TIME 0xffff

// start and stop bytes for the UART protocol
static const uint8_t startByte = 0xAA,
					 stopByte  = 0x55;

static const uint16_t displaySizeX = 320,
					  displaySizeY = 240;

static const uint16_t
		centerX = 160,
		centerY = 120;

QueueHandle_t ESPL_RxQueue; // Already defined in ESPL_Functions.h
SemaphoreHandle_t ESPL_DisplayReady;

double velocity[] = {80, 0};
double position[] = {100, 10};

float collisionNormal[] = {0, 0};

collision_rect collisionRects[20] = {};
uint8_t collisionRectsCount = 0;

collision_circle collisionCircles[20] = {};
uint8_t collisionCirclesCount = 0;

collision_poly collisionPolygons[20] = {};
uint8_t collisionPolygonsCount = 0;

//uint16_t collisionObjects[200];
//uint8_t collisionObjectsIndex = 0;
double collisionSpeedMultiplier = 1.1;

void drawTask() {
	TickType_t xLastWakeTime;
	TickType_t xWakeTime;
	xWakeTime = xTaskGetTickCount();
	xLastWakeTime = xWakeTime;

	font_t font = gdispOpenFont("DejaVuSans24*");
	char str[100];

	gdispClear(White);

	//drawBitmap(background, 320, 240);

	registerCollisionRectangle(0, 236, 320, 4);
	registerCollisionRectangle(180, 120, 320, 4);

	while(TRUE) {
		calculatePhysics(xWakeTime - xLastWakeTime);
		drawBall();
		gdispFillArea(0, 236, 320, 4, Blue);
		gdispFillArea(180, 120, 320, 4, Blue);

		//Wait for display to stop writing
		xSemaphoreTake(ESPL_DisplayReady, portMAX_DELAY);
		ESPL_DrawLayer();

		xLastWakeTime = xWakeTime;

		//50Hz
		vTaskDelayUntil(&xWakeTime, 1000/50);
		gdispClear(White);

		//Calculate and show FPS
		uint16_t delay = xWakeTime - xLastWakeTime;
		uint8_t fps = 1000 / delay;
		sprintf( str, "FPS: %2d", fps);
		gdispDrawString(10, 10, str, font, Black);
	}
}

void calculatePhysics(int deltaTime) {
	double deltaSeconds = ((double) deltaTime) / 1000.0;

	//Add gravity to velocity
	velocity[1] += GRAVITY * deltaSeconds;

	//Update position based on velocity
	int16_t totalDeltaX = (velocity[0] * deltaSeconds);
	int16_t totalDeltaY = (velocity[1] * deltaSeconds);

	uint16_t newPositionX = position[0];
	uint16_t newPositionY = position[1];

	if (checkCollision(position[0] + totalDeltaX, position[1] + totalDeltaY)) {
		uint16_t numberSteps = 0;
		if (totalDeltaX > totalDeltaY) {
			numberSteps = totalDeltaX;
		} else {
			numberSteps = totalDeltaY;
		}

		int16_t dx = 0;
		int16_t dy = 0;
		for (uint16_t steps = 0; steps <= numberSteps; steps++) {
			int16_t newDx = totalDeltaX / numberSteps * steps;
			int16_t newDy = totalDeltaY / numberSteps * steps;
			if (checkCollision(position[0] + newDx, position[1] + newDy)) {
				newPositionX += dx;
				newPositionY += dy;
				break;
			}

			dx = newDx;
			dy = newDy;
		}
		double dot = DOT_PRODUCT(velocity, collisionNormal);
		velocity[0] = velocity[0] - 2 * collisionSpeedMultiplier * dot * collisionNormal[0];
		velocity[1] = velocity[1] - 2 * collisionSpeedMultiplier * dot * collisionNormal[1];

	} else {
		newPositionX += totalDeltaX;
		newPositionY += totalDeltaY;
	}

	position[0] = newPositionX;
	position[1] = newPositionY;
}

uint8_t checkCollision(uint16_t positionX, uint16_t positionY) {
	for (int i = 0; i < collisionRectsCount; i++) {
		if (checkRectangleCollision(positionX, positionY, &collisionRects[i])) {
			return TRUE;
		}
	}

	for (int i = 0; i < collisionCirclesCount; i++) {
		if (checkCircleCollision(positionX, positionY, &collisionCircles[i])) {
			return TRUE;
		}
	}

	for (int i = 0; i < collisionPolygonsCount; i++) {
		if (checkPolygonCollision(positionX, positionY, &collisionPolygons[i])) {
			return TRUE;
		}
	}

	return FALSE;
}

uint8_t checkRectangleCollision(uint16_t positionX, uint16_t positionY, collision_rect *rect) {
	uint8_t xCollides = positionX + BALL_RADIUS >= rect->x && positionX - BALL_RADIUS <= rect->x + rect->width;
	uint8_t yCollides = positionY + BALL_RADIUS >= rect->y && positionY - BALL_RADIUS <= rect->y + rect->height;
	return xCollides && yCollides;
}

uint8_t checkCircleCollision(uint16_t positionX, uint16_t positionY, collision_circle *circle) {
	return abs(circle->x - positionX) <= circle->radius && abs(circle->y - positionY) <= circle->radius;
}

uint8_t checkPolygonCollision(uint16_t positionX, uint16_t positionY, collision_poly *poly) {
	for (uint8_t lineIndex = 0; lineIndex < poly->pointCount; lineIndex++) {
		uint16_t *p1;
		uint16_t *p2;

 		if (lineIndex == 0) {
			p1 = &poly->points[0];
			p2 = &poly->points[(poly->pointCount - 1) * 2];
		} else {
			p1 = &poly->points[lineIndex * 2];
			p2 = &poly->points[(lineIndex + 1) * 2];
		}

		if (checkLineCollision(positionX, positionY, p1[0], p1[1], p2[0], p2[1])) {
			collisionNormal[0] = -(p2[1] - p1[1]);
			collisionNormal[1] = p2[0] - p1[0];

			collisionNormal[0] = -collisionNormal[0] / LEN(collisionNormal);
			collisionNormal[1] = -collisionNormal[1] / LEN(collisionNormal);
			return TRUE;
		}
	}

	return FALSE;
}

uint8_t checkLineCollision(uint16_t positionX, uint16_t positionY, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
	//Get length of the line
	float lineLen = DIST(x1, y1, x2, y2);

	//Get dot product of the line and circle
	float dot = (((positionX - x1) * (x2 - x1)) + ((positionY - y1) * (y2 - y1))) / pow(lineLen, 2);

	//Find the closest point on the line
	float closestX = x1 + (dot * (x2 - x1));
	float closestY = y1 + (dot * (y2 - y1));

	//Get distance from the point to the two ends of the line
	float d1 = DIST(closestX, closestY, x1, y1);
	float d2 = DIST(closestX, closestY, x2, y2);

	//Since floats are so minutely accurate, add a little buffer zone that will give collision
	float buffer = 0.1; // higher # = less accurate

	//If the two distances are equal to the line's length, the point is on the line!
	//note we use the buffer here to give a range, rather than one #
	uint8_t pointOnLine = d1 + d2 >= lineLen - buffer && d1 + d2 <= lineLen + buffer;

	//Is the closest point is within the ball?
	uint8_t pointInBall = DIST(closestX, closestY, positionX, positionY) <= BALL_RADIUS;

	return pointOnLine && pointInBall;
}

void drawBall() {
	gdispFillCircle(position[0], position[1], BALL_RADIUS, Red);
}

void drawBitmap(uint8_t bitmap[], uint16_t width, uint16_t height) {
	for (uint16_t x = 0; x < width - 1; x++) {
		for (uint16_t y = 0; y < height - 1; y++) {
			if (bitmap[x + y * width] < 255) {
				gdispDrawPixel(x, y, Blue);
			}
		}
	}
}

void registerCollisionCircle(uint16_t x, uint16_t y, uint8_t radius) {
	collision_circle *circle = &collisionCircles[collisionCirclesCount++];
	circle->x = x;
	circle->y = y;
	circle->radius = radius;
}

void registerCollisionLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
	collision_poly *poly = &collisionPolygons[collisionPolygonsCount++];
	poly->pointCount = 2;
	poly->points[0] = x1;
	poly->points[1] = y1;
	poly->points[2] = x2;
	poly->points[3] = y2;
}

void registerCollisionRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
	collision_poly *poly = &collisionPolygons[collisionPolygonsCount++];
	poly->pointCount = 4;
	poly->points[0] = x;
	poly->points[1] = y;
	poly->points[2] = x + width;
	poly->points[3] = y;
	poly->points[4] = x + width;
	poly->points[5] = y + height;
	poly->points[6] = x;
	poly->points[7] = y + height;
}

void registerCollisionPolygon(point *points, uint8_t pointCount) {
	collision_poly *poly = &collisionPolygons[collisionPolygonsCount++];
	poly->pointCount = pointCount;

	for (int i = 0; i < pointCount; i++) {
		poly->points[i * 2] = points[i].x;
		poly->points[i * 2 + 1] = points[i].y;
	}
}

int main() {
	// Initialize Board functions and graphics
	ESPL_SystemInit();

	// Initializes Draw Queue with 100 lines buffer
	//JoystickQueue = xQueueCreate(100, 2 * sizeof(char));

	xTaskCreate(drawTask, "drawTask", 5000, NULL, 5, NULL);

	// Start FreeRTOS Scheduler
	vTaskStartScheduler();
}

/*
 *  Hook definitions needed for FreeRTOS to function.
 */
void vApplicationIdleHook() {
	while (TRUE) {
	};
}

void vApplicationMallocFailedHook() {
	while(TRUE) {
	};
}