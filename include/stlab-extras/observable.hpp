#ifndef _OBSERVABLE_H_
#define _OBSERVABLE_H_
#include <bitset>
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <iostream>
#include <list>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/serial_queue.hpp>

namespace stlab_extras {
template <typename T> class observable;
namespace detail {
template <typename Sig> struct observable_signature_helper;
template <typename... T> class subscriber {
public:
  virtual void on_value(T... value) = 0;
  virtual void on_end(std::exception_ptr) = 0;
};

template <> class subscriber<void> {
public:
  virtual void on_value() = 0;
  virtual void on_end(std::exception_ptr) = 0;
};

template <typename... T> struct value_cache {
  boost::optional<std::tuple<T...>> storage;

  value_cache() : storage(boost::none) {}

  inline void set(T... args) { storage = std::make_tuple(args...); }

  inline void reset() { storage = boost::none; }

  inline void call_subscriber(std::shared_ptr<subscriber<T...>> &sub) {
    std::cout << "Attempting to call with storage with multiple types :("
              << std::endl;
  }
};

template <> struct value_cache<void> {
  bool storage;

  value_cache() : storage(false) {}

  inline void set() { storage = true; }
  inline void reset() { storage = false; }
  inline void call_subscriber(std::shared_ptr<subscriber<void>> &sub) {
    if (storage) {
      sub->on_value();
    }
  }
};

template <typename T> struct value_cache<T> {
  boost::optional<T> storage;

  value_cache() : storage(boost::none) {}

  inline void set(T value) { storage = std::move(value); }

  inline void reset() { storage = boost::none; }

  inline void call_subscriber(std::shared_ptr<subscriber<T>> &sub) {
    if (storage) {
      sub->on_value(*storage);
    }
  }
};

class subscription {};
template <typename... T>
class shared_observable
    : public std::enable_shared_from_this<shared_observable<T...>> {
public:
  // Executor where everything will be scheduled
  stlab::executor_t executor_;
  // Last value received (or boost::none, if none have been received yet)
  value_cache<T...> last_value_;
  boost::optional<std::exception_ptr> end_state_;
  /** Four states:
   * Open (end_state_ = boost::none)
   * Closed (end_state_ = nullptr)
   * Exception (end_state != nullptr, last_value_ should be cleared)
   */
  // What dependencies to keep alive.
  std::list<std::shared_ptr<void>> dependencies_;

  struct observable_subscription;
  typedef std::list<std::weak_ptr<observable_subscription>>
      subscription_list_type;
  subscription_list_type subscriptions_;

  class observable_subscription : public subscription {
  public:
    std::shared_ptr<subscriber<T...>> subscriber_;
    std::shared_ptr<shared_observable<T...>> owner_;
    boost::optional<typename subscription_list_type::iterator> me_;

    observable_subscription(std::shared_ptr<subscriber<T...>> subscriber)
        : subscriber_(std::move(subscriber)), owner_(nullptr),
          me_(boost::none) {}

    ~observable_subscription() {
      if (owner_ && me_) {
        // Schedule my removal from the list of subscribers.
        // It is OK if we linger in there for the time being, as the
        // subscriptions_ is a list of weak pointers.
        owner_->executor_(
            [ owner(owner_), it(*me_) ]() { owner->subscriptions_.erase(it); });
      }
    }
  };

  // ALWAYS! call this from executor_
  void end(std::exception_ptr exc) {
    if (end_state_) {
      // observable was already stopped
      return;
    }
    end_state_ = exc;
    if (exc != nullptr) {
      last_value_.reset();
    }
    // Get all alive subscriptions
    std::list<std::shared_ptr<observable_subscription>> shared_subscriptions;
    for (auto sub : subscriptions_) {
      auto shared = sub.lock();
      if (shared) {
        shared_subscriptions.emplace_back(std::move(shared));
      }
    }
    // Remove all active subscriptions from the list, and reset owner_.
    // Any subscribers that have been destroyed in the mean time,
    // will have scheduled a task to do this in the future.
    for (auto &sub : shared_subscriptions) {
      boost::optional<typename subscription_list_type::iterator> iter =
          boost::none;
      std::swap(sub->me_, iter);
      if (iter) {
        subscriptions_.erase(*iter);
      }
      sub->owner_.reset();
    }
    for (auto &sub : shared_subscriptions) {
      // Automatically resets sub->subscriber_, while giving us a reference.
      std::shared_ptr<subscriber<T...>> subscriber(std::move(sub->subscriber_));
      if (subscriber) {
        try {
          subscriber->on_end(exc);
        } catch (...) {
        }
      }
    }
    // Drop all references to dependencies
    dependencies_.clear();
  }

  // ALWAYS! call this from executor_!
  template <typename... X> void publish(X... values) {
    if (end_state_) {
      // observable was already ended
      return;
    }
    last_value_.set(values...);
    // Get all alive subscriptions
    std::list<std::shared_ptr<observable_subscription>> shared_subscriptions;
    for (auto sub : subscriptions_) {
      auto shared = sub.lock();
      if (shared) {
        shared_subscriptions.emplace_back(std::move(shared));
      }
    }
    // Trigger on_value
    for (auto &sub : shared_subscriptions) {
      if (sub->subscriber_) {
        try {
          sub->subscriber_->on_value(values...);
        } catch (...) {
        }
      }
    }
  }

public:
  template <typename E>
  shared_observable(E &&e)
      : executor_(stlab::serial_queue_t(std::forward<E>(e)).executor()),
        end_state_(boost::none) {
    std::cout << "shared_observable()" << std::endl;
  }
  ~shared_observable() { std::cout << "~shared_observable()" << std::endl; }

  std::shared_ptr<subscription> subscribe(std::shared_ptr<subscriber<T...>> s,
                                          bool repeat_last_value) {
    auto sub = std::make_shared<observable_subscription>(std::move(s));
    executor_([ _this(this->shared_from_this()), sub, repeat_last_value ]() {
      if (repeat_last_value && sub->subscriber_) {
        try {
          _this->last_value_.call_subscriber(sub->subscriber_);
        } catch (...) {
        }
      }
      if (_this->end_state_) {
        // Automatically resets subscriber_, but keeps a copy.
        std::shared_ptr<subscriber<T...>> s(std::move(sub->subscriber_));
        // (No need to remove from list, touch owner_ or me_, as these will not
        // have been set yet)
        if (s) {
          try {
            s->on_end(*(_this->end_state_));
          } catch (...) {
          }
        }
      } else {
        sub->owner_ = _this;
        sub->me_ =
            _this->subscriptions_.emplace(_this->subscriptions_.end(), sub);
      }
    });
    return sub;
  }

  template <typename T2> friend class stlab_extras::observable;
  template <typename Sig>
  friend struct stlab_extras::detail::observable_signature_helper;
};

/*
template <typename T, typename... Args> class transforming_observable;
template <typename R> struct transform_helper {
  template <typename... Args>
  static inline void publish_transformed(
      std::shared_ptr<transforming_observable<R, Args...>> &_this,
      Args... args) {
    _this->publish(_this->f_(std::move(args)...));
  }
};
template <> struct transform_helper<void> {
  template <typename... Args>
  static inline void publish_transformed(
      std::shared_ptr<transforming_observable<void, Args...>> &_this,
      Args... args) {
    _this->f_(std::move(args)...);
    _this->publish();
  }
};

template <typename T, typename... Args>
class transforming_observable : public shared_observable<T> {
private:
  std::function<T(Args...)> f_;
  std::shared_ptr<void> subscription_reference_; // Possibly keep a reference to
                                                 // the subscription, to keep
                                                 // the chain alive.
  friend class transform_helper<T>;

  class transforming_subscriber : public subscriber<Args...> {
  private:
    std::weak_ptr<transforming_observable<T, Args...>> owner_;

  public:
    transforming_subscriber(
        std::weak_ptr<transforming_observable<T, Args...>> owner)
        : owner_(std::move(owner)) {}
    ~transforming_subscriber() { on_end(nullptr); }
    void on_value(Args... args) override {
      // First lock to get the executor,
      // but then pass a weak ptr to the task, as it could still be dropped.
      if (auto owner = owner_.lock()) {
        owner->executor_([ weak_owner(owner_), args... ]() {
          if (auto owner = weak_owner.lock()) {
            try {
              transform_helper<T>::publish_transformed(owner, args...);
            } catch (...) {
              owner->end(std::current_exception());
              owner->subscription_reference_.reset();
            }
          }
        });
      }
    }
    void on_end(std::exception_ptr exc) override {
      if (auto owner = owner_.lock()) {
        owner->executor_([ weak_owner(owner_), exc ]() {
          if (auto owner = weak_owner.lock()) {
            owner->end(exc);
            owner->subscription_reference_.reset();
          }
        });
      }
    }
  };

public:
  template <typename E, typename F>
  transforming_observable(E &&e, F &&f)
      : shared_observable<T>(std::forward<E>(e)), f_(std::forward<F>(f)) {}

  template <typename E, typename F>
  static std::pair<std::shared_ptr<subscriber<Args...>>,
                   std::shared_ptr<shared_observable<T>>>
  create_empty(E &&e, F &&f) {
    auto obs = std::make_shared<transforming_observable>(std::forward<E>(e),
                                                         std::forward<F>(f));
    auto sub = std::make_shared<transforming_subscriber>(obs);
    return std::make_pair(sub, obs);
  }

  template <typename E, typename F>
  static std::shared_ptr<shared_observable<T>>
  create(E &&e, std::shared_ptr<shared_observable<Args...>> source,
         bool repeat_last_value, F &&f) {
    auto obs = std::make_shared<transforming_observable>(std::forward<E>(e),
                                                         std::forward<F>(f));
    obs->subscription_reference_ = source->subscribe(
        std::make_shared<transforming_subscriber>(obs), repeat_last_value);
    return obs;
  }
};
*/

template <typename R, typename... Args>
struct observable_signature_helper<R(Args...)> {
  typedef std::function<void(Args...)> PublishType;
  typedef std::function<R(Args...)> ProcessType;
  typedef R ReturnType;
  static PublishType
  create_publish(std::weak_ptr<shared_observable<ReturnType>> weak_observer,
                 ProcessType process) {
    return [
      weak_observer{std::move(weak_observer)}, process{std::move(process)}
    ](Args... args) {
      if (auto observer = weak_observer.lock()) {
        std::tuple<Args...> argtuple(std::move(args)...);
        observer->executor_([
          weak_observer, argtuple{std::move(argtuple)}, process
        ]() mutable {
          if (weak_observer.expired()) {
            return;
          }
          try {
            R ret =
                stlab::v1::detail::apply_tuple(process, std::move(argtuple));
            if (auto observer = weak_observer.lock()) {
              observer->publish(std::move(ret));
            }

          } catch (...) {
            if (auto observer = weak_observer.lock()) {
              observer->end(std::current_exception());
            }
          }
        });
      }
    };
  }
};
template <typename I, typename O>
class transforming_subscriber
    : public subscriber<I>,
      public std::enable_shared_from_this<transforming_subscriber<I, O>> {
private:
  std::function<O(I)> transform_;
  std::weak_ptr<shared_observable<O>> weak_output_;

public:
  void on_value(I input) override {
    if (auto output = weak_output_.lock()) {
      output->executor_([
        input{std::move(input)}, _this(this->shared_from_this())
      ]() mutable {
        if (_this->weak_output_.expired()) {
          return;
        }
        try {
          auto r = _this->transform_(std::move(input));
          if (auto output = _this->weak_output_.lock()) {
            output->publish(r);
          }
        } catch (...) {
          if (auto output = _this->weak_output_.lock()) {
            output->end(std::current_exception());
          }
        }
      });
    }
  }
  void on_end(std::exception_ptr ex) override {
    if (auto output = weak_output_.lock()) {
      auto weak_output = weak_output_;
      output->executor_([ex, weak_output]() {
        if (auto output = weak_output.lock()) {
          output->end(ex);
        }
      });
    }
  }

  template <typename F>
  transforming_subscriber(std::weak_ptr<shared_observable<O>> output, F &&f)
      : transform_(f), weak_output_(std::move(output)) {}
};
} // namespace detail

template <typename T> class observable {
public:
  observable() {}
  // TODO: Lock this constructor down more.
  observable(std::shared_ptr<detail::shared_observable<T>> base)
      : base_(std::move(base)) {}

  template <typename E, typename F>
  observable<typename std::result_of<F(T)>::type> map(E &&e, F &&f) {
    typedef typename std::result_of<F(T)>::type ReturnType;
    if (!base_) {
      return observable<ReturnType>();
    }
    auto next = std::make_shared<detail::shared_observable<ReturnType>>(
        std::forward<E>(e));
    auto transform =
        std::make_shared<detail::transforming_subscriber<T, ReturnType>>(
            next, std::forward<F>(f));
    auto sub = base_->subscribe(transform, true);
    next->dependencies_.emplace_back(std::move(sub));
    return observable<ReturnType>(next);
  }

private:
  std::shared_ptr<detail::shared_observable<T>> base_;
};

template <typename Sig, typename E, typename F>
std::pair<
    typename detail::observable_signature_helper<Sig>::PublishType,
    observable<typename detail::observable_signature_helper<Sig>::ReturnType>>
create_observable(E &&e, F &&f) {
  typedef detail::observable_signature_helper<Sig> signature_helper;
  auto base = std::make_shared<
      detail::shared_observable<typename signature_helper::ReturnType>>(
      std::forward<E>(e));
  auto func = signature_helper::create_publish(base, std::forward<F>(f));
  return std::make_pair(
      func, observable<typename signature_helper::ReturnType>(base));
}
} // namespace stlab_extras
#endif //_OBSERVABLE_H_
