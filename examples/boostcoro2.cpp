#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <iostream>
#include <stlab-extras/asio_executor.hpp>
#include <stlab-extras/boost_coro2.hpp>

/**
 * This example consists of:
 * - an asynchronous sleep function
 * - a SlowAdd function that will (asynchronously) sleep for both it's arguments, before returning the result.
 * - a ParallelAdd function that will call SlowAdd twice in parallel
 * 
 * Don't forget to notice that this is a single-threaded application,
 * everything is scheduled on the io service.
 * Yet while one asynchronous call is waiting, others will be scheduled.
 */

/* 
 * Schedules a timer on the io_service that expires after 'seconds' seconds
 * Mostly as a stand-in for a slow asynchronous function.
 */
stlab::future<void> sleep(boost::asio::io_service &io_service, int seconds) {
  auto timer = std::make_shared<boost::asio::deadline_timer>(
      io_service, boost::posix_time::seconds(seconds));
  auto package = stlab::package<void(const boost::system::error_code)>(
      stlab_extras::asio_executor(io_service),
      [timer](const boost::system::error_code &error) {
        if (error) {
          throw error;
        }
      });
  timer->async_wait(package.first);
  return package.second;
}

/**
 * SlowAdd: Waits for a seconds, then for b seconds, and then returns a + b
 * This should be run with stlab_extras::boost_coro2::Run, and it's return type will then be stlab::future<int>
 * It's a simple coroutine, not using any asynchronous results
 */
int SlowAdd(stlab_extras::boost_coro2::Await await,
            boost::asio::io_service &io_service, int a, int b) {
  std::cout << "SlowAdd(" << a << ", " << b << ")" << std::endl;
  await(sleep(io_service, a));
  std::cout << "Timer for a = " << a << " elapsed" << std::endl;
  await(sleep(io_service, b));
  std::cout << "Timer for b = " << b << " elasped, returning!" << std::endl;
  return a + b;
}

/**
 * ParallelAdd: SlowAdds a + b  and c + d in parallel!
 */
std::pair<int, int> ParallelAdd(stlab_extras::boost_coro2::Await await, boost::asio::io_service& io_service, int a, int b, int c, int d) {
	auto first_result = stlab_extras::boost_coro2::Run(stlab_extras::asio_executor(io_service), &SlowAdd, std::ref(io_service), a, b);
	auto second_result = stlab_extras::boost_coro2::Run(stlab_extras::asio_executor(io_service), &SlowAdd, std::ref(io_service), c, d);
	// Note how you can use await as part of an expression!
	return std::make_pair(await(first_result), await(second_result));
}

int main() {
  boost::asio::io_service io_service;
  boost::asio::io_service::work work(io_service);

  auto result = stlab_extras::boost_coro2::Run(
      stlab_extras::asio_executor(io_service), &ParallelAdd, std::ref(io_service), 1, 2, 3, 4);
  result.recover([&io_service](auto f) {
    try {
	auto result = *f.get_try();
	std::cout << "Results: " << result.first << " and " << result.second << std::endl;
    } catch (const std::exception &ex) {
      std::cerr << ex.what() << std::endl;
    }
	io_service.stop();
  }).detach();
  io_service.run();
  return 0;
}
