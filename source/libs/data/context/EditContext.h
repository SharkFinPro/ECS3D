#ifndef EDITCONTEXT_H
#define EDITCONTEXT_H

class EditContext {
public:
  virtual ~EditContext() = default;

  // TODO: expose the services the editor panels need: current selection, the project
  // TODO:   serializer (save/save-as/new), and the asset registry/browser. Mutations made
  // TODO:   here become edit commands sent to the server, not direct scene edits.
};



#endif //EDITCONTEXT_H
