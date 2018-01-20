#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <iostream>
#include <stlab-extras/asio_executor.hpp>
#include <stlab-extras/observable.hpp>

int main() {
  boost::asio::io_service io_service;
  boost::asio::io_service::work work(io_service);

  auto obs = stlab_extras::create_observable<int(int)>(
      stlab_extras::asio_executor(io_service), [](int n) {
        std::cout << "Observer function called: " << n << std::endl;
        return n;
      });
  auto obsplus =
      obs.second.map(stlab_extras::asio_executor(io_service), [](int n) {
        std::cout << "obsplus called: " << n << std::endl;
        return n + 5;
      });
  auto obspow = obsplus.map(stlab_extras::asio_executor(io_service), [](int n) {
    std::cout << "obspow called: " << n << std::endl;
    return n * n;
  });
  auto obsend =
      obspow.map(stlab_extras::asio_executor(io_service), [](int n) -> void {
        std::cout << "obsend called: " << n << std::endl;
      });
  auto obsvoid =
      obsend.map(stlab_extras::asio_executor(io_service),
                 []() -> void { std::cout << "obsvoid called!" << std::endl; });

  std::cout << "Calling observable function!" << std::endl;
  obs.first(123);
  obs.first(456);
  std::cout << "Running IO loop" << std::endl;
  io_service.run();
  return 0;
}
