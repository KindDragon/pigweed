.. _module-pw_async2:

=============
pw_async2
=============
.. pigweed-module::
   :name: pw_async2

   - **Simple Ownership**: Say goodbye to that jumble of callbacks and shared
     state! Complex tasks with many concurrent elements can be expressed by
     simply combining smaller tasks.
   - **Efficient**: No dynamic memory allocation required.
   - **Pluggable**: Your existing event loop, work queue, or task scheduler
     can run the ``Dispatcher`` without any extra threads.
   - **Coroutine-capable**: C++20 coroutines and Rust ``async fn`` work just
     like other tasks, and can easily plug into an existing ``pw_async2``
     systems.

:cpp:class:`pw::async2::Task` is Pigweed's async primitive. ``Task`` objects
are cooperatively-scheduled "threads" which yield to the
:cpp:class:`pw::async2::Dispatcher` when waiting. When the ``Task`` is able to make
progress, the ``Dispatcher`` will run it again. For example:

.. code-block:: cpp

   #include "pw_async2/dispatcher.h"
   #include "pw_async2/poll.h"

   #include "pw_result/result.h"

   using ::pw::async2::Context;
   using ::pw::async2::Poll;
   using ::pw::async2::Ready;
   using ::pw::async2::Pending;
   using ::pw::async2::Task;

   class ReceiveAndSend : public Task {
    public:
     ReceiveAndSend(Receiver receiver, Sender sender):
       receiver_(receiver), sender_(sender) {}

     Poll<> Pend(Context& cx) {
       if (!send_future_) {
         // ``PendReceive`` checks for available data or errors.
         //
         // If no data is available, it will grab a ``Waker`` from
         // ``cx.Waker()`` and return ``Pending``. When data arrives,
         // it will call ``waker.Wake()`` which tells the ``Dispatcher`` to
         // ``Pend`` this ``Task`` again.
         Poll<pw::Result<Data>> new_data = receiver_.PendReceive(cx);
         if (new_data.is_pending()) {
           // The ``Task`` is still waiting on data. Return ``Pending``,
           // yielding to the dispatcher. ``Pend`` will be called again when
           // data becomes available.
           return Pending();
         }
         if (!new_data->ok()) {
           PW_LOG_ERROR("Receiving failed: %s", data->status().str());
           // The ``Task`` completed;
           return Ready();
         }
         Data& data = **new_data;
         send_future_ = sender_.Send(std::move(data));
       }
       // ``PendSend`` attempts to send ``data_``, returning ``Pending`` if
       // ``sender_`` was not yet able to accept ``data_``.
       Poll<pw::Status> sent = send_future_.Pend(cx);
       if (sent.is_pending()) {
         return Pending();
       }
       if (!sent->ok()) {
         PW_LOG_ERROR("Sending failed: %s", sent->str());
       }
       return Ready();
     }
    private:
     Receiver receiver_;
     Sender sender_;

     // ``SendFuture`` is some type returned by `Sender::Send` that offers a
     // ``Pend`` method similar to the one on ``Task``.
     std::optional<SendFuture> send_future_ = std::nullopt;
   };

Tasks can then be run on a :cpp:class:`pw::async2::Dispatcher` using the
:cpp:func:`pw::async2::Dispatcher::Post` method:

.. code-block:: cpp

   #include "pw_async2/dispatcher.h"

   int main() {
     ReceiveAndSendTask task(SomeMakeReceiverFn(), SomeMakeSenderFn());
     Dispatcher dispatcher;
     dispatcher.Post(task);
     dispatcher.RunUntilComplete(task);
     return 0;
   }

.. _module-pw_async2-coroutines:

----------
Coroutines
----------
C++20 users can also define tasks using coroutines!

.. literalinclude:: examples/coro.cc
   :language: cpp
   :linenos:
   :start-after: [pw_async2-examples-coro-injection]
   :end-before: [pw_async2-examples-coro-injection]

Any value with a ``Poll<T> Pend(Context&)`` method can be passed to
``co_await``, which will return with a ``T`` when the result is ready.

To return from a coroutine, ``co_return <expression>`` must be used instead of
the usual ``return <expression>`` syntax. Because of this, the
:c:macro:`PW_TRY` and :c:macro:`PW_TRY_ASSIGN` macros are not usable within
coroutines. :c:macro:`PW_CO_TRY` and :c:macro:`PW_CO_TRY_ASSIGN` should be
used instead.

For a more detailed explanation of Pigweed's coroutine support, see the
documentation on the :cpp:class:`pw::async2::Coro<T>` type.

------
Timing
------
When using ``pw::async2``, timing functionality should be injected
by accepting a :cpp:class:`pw::async2::TimeProvider` (most commonly
``TimeProvider<SystemClock>`` when using the system's built-in `time_point`
and `duration` types).

:cpp:class:`pw::async2::TimeProvider` allows for easily waiting
for a timeout or deadline using the
:cpp:func:`pw::async2::TimePoint::WaitFor` and
:cpp:func:`pw::async2::TimePoint::WaitUntil` methods.

Additionally, code which uses :cpp:class:`pw::async2::TimeProvider` for timing
can be tested with simulated time using
:cpp:class:`pw::async2::SimulatedTimeProvider`. Doing so helps avoid
timing-dependent test flakes, as well as ensure that tests are fast since they
don't need to wait for real-world time to elapse.

.. doxygenclass:: pw::async2::TimeProvider
   :members:

.. doxygenfunction:: pw::async2::GetSystemTimeProvider

.. doxygenclass:: pw::async2::SimulatedTimeProvider
   :members:

-----------------
C++ API reference
-----------------
.. doxygenclass:: pw::async2::Task
  :members:

.. doxygenclass:: pw::async2::Poll
  :members:

.. doxygenfunction:: pw::async2::Ready()

.. doxygenfunction:: pw::async2::Ready(std::in_place_t, Args&&... args)

.. doxygenfunction:: pw::async2::Ready(T&& value)

.. doxygenfunction:: pw::async2::Pending()

.. doxygenclass:: pw::async2::Context
  :members:

.. doxygenclass:: pw::async2::Waker
  :members:

.. doxygenclass:: pw::async2::Dispatcher
  :members:

.. doxygenclass:: pw::async2::Coro
  :members:

.. doxygenclass:: pw::async2::CoroContext
  :members:

-------------
C++ Utilities
-------------
.. doxygenfunction:: pw::async2::AllocateTask(pw::allocator::Allocator& allocator, Pendable&& pendable)

.. doxygenfunction:: pw::async2::AllocateTask(pw::allocator::Allocator& allocator, Args&&... args)

.. doxygenclass:: pw::async2::CoroOrElseTask
  :members:

.. doxygenclass:: pw::async2::PendFuncTask
  :members:

.. doxygenclass:: pw::async2::PendableAsTask
  :members:


.. doxygenfunction:: pw::async2::MakeOnceSenderAndReceiver

.. doxygenclass:: pw::async2::OnceSender
  :members:

.. doxygenclass:: pw::async2::OnceReceiver
  :members:

.. doxygenfunction:: pw::async2::MakeOnceRefSenderAndReceiver

.. doxygenclass:: pw::async2::OnceRefSender
  :members:

.. doxygenclass:: pw::async2::OnceRefReceiver
  :members:

.. toctree::
   :hidden:
   :maxdepth: 1

   Backends <backends>
