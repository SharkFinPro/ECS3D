#include "BindingContext.h"

ObjectManager* BindingContext::s_objectManager = nullptr;

void BindingContext::setObjectManager(ObjectManager* objectManager)
{
  s_objectManager = objectManager;
}

ObjectManager* BindingContext::getObjectManager()
{
  return s_objectManager;
}
