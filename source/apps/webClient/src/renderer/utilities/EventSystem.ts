// Port of source/utilities/EventSystem.h — the header-only templated event system.
//
// C++ keys listeners by event *type* through a std::tuple of per-type vectors; TypeScript has no
// such type-level dispatch, so the same shape is expressed as an event *map* interface keyed by
// name. `on()` still returns an EventListener handle that `removeListener()` consumes, so call
// sites read the same as `m_framebufferResizeEventListener` upstream.

export interface EventListener<M, K extends keyof M> {
  type: K;
  callback: ((event: M[K]) => void) | null;
}

export class EventSystem<M> {
  private listeners = new Map<keyof M, ((event: never) => void)[]>();

  on<K extends keyof M>(type: K, callback: (event: M[K]) => void): EventListener<M, K> {
    const forType = this.listeners.get(type) ?? [];
    forType.push(callback as (event: never) => void);
    this.listeners.set(type, forType);

    return { type, callback };
  }

  emit<K extends keyof M>(type: K, event: M[K]): void {
    for (const callback of this.listeners.get(type) ?? []) {
      (callback as unknown as (e: M[K]) => void)(event);
    }
  }

  removeListener<K extends keyof M>(listener: EventListener<M, K>): void {
    const forType = this.listeners.get(listener.type);
    if (forType && listener.callback) {
      const index = forType.indexOf(listener.callback as (event: never) => void);
      if (index >= 0) forType.splice(index, 1);
    }

    listener.callback = null;
  }
}
