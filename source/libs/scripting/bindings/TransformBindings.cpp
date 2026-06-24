#include "TransformBindings.h"

TransformBindings TransformBindingsProvider::getBindings()
{
  return TransformBindings {
    .getPosition = &bindGetPosition,
    .getScale = &bindGetScale,
    .getRotation = &bindGetRotation,
    .setScale = &bindSetScale,
    .setRotation = &bindSetRotation,
    .move = &bindMove,
    .start = &bindStart,
    .stop = &bindStop
  };
}

void TransformBindingsProvider::bindGetPosition(const char* uuid, float* x, float* y, float* z)
{
  // TODO: resolve the Transform for uuid (find), then write transform->getPosition() into x/y/z.
  (void)uuid;
  (void)x;
  (void)y;
  (void)z;
}

void TransformBindingsProvider::bindGetScale(const char* uuid, float* x, float* y, float* z)
{
  // TODO: write transform->getScale() into x/y/z.
  (void)uuid;
  (void)x;
  (void)y;
  (void)z;
}

void TransformBindingsProvider::bindGetRotation(const char* uuid, float* x, float* y, float* z)
{
  // TODO: write transform->getRotation() into x/y/z.
  (void)uuid;
  (void)x;
  (void)y;
  (void)z;
}

void TransformBindingsProvider::bindSetScale(const char* uuid, float x, float y, float z)
{
  // TODO: transform->setScale({ x, y, z }).
  (void)uuid;
  (void)x;
  (void)y;
  (void)z;
}

void TransformBindingsProvider::bindSetRotation(const char* uuid, float x, float y, float z)
{
  // TODO: transform->setRotation({ x, y, z }).
  (void)uuid;
  (void)x;
  (void)y;
  (void)z;
}

void TransformBindingsProvider::bindMove(const char* uuid, float x, float y, float z)
{
  // TODO: transform->move({ x, y, z }).
  (void)uuid;
  (void)x;
  (void)y;
  (void)z;
}

void TransformBindingsProvider::bindStart(const char* uuid)
{
  // TODO: transform->start().
  (void)uuid;
}

void TransformBindingsProvider::bindStop(const char* uuid)
{
  // TODO: transform->stop().
  (void)uuid;
}
