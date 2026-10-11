#ifndef OBJECTTREESORTMODE_H
#define OBJECTTREESORTMODE_H

namespace objectTreeOrder {
  // How the tree orders siblings for display. This never touches the scene: authored is the order
  // ObjectManager/Object already hand out (today, load order - there is no persisted sibling order yet),
  // and alphabetical is a display-only sorted copy built fresh each frame.
  enum class SortMode { authored, alphabetical };
}

#endif //OBJECTTREESORTMODE_H
