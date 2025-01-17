#include "clintrinsics/connection.hpp"
#include "clintrinsics/crypto.hpp"
#include "clintrinsics/database.hpp"

#include <string>

clintrinsics::Task<std::string> testco(std::string s, uint32_t delayMillisec)
{
   std::string result;
   for (int i = 0; i < 10; ++i)
   {
      printf("s = \"%s\", i = %d\n", s.c_str(), i);
      result += "(" + s + ")";
      co_await clintrinsics::later(delayMillisec);
   }
   printf("s = \" %s \", finished\n", s.c_str());
   co_return result;
}

clintrinsics::Task<> testco2(uint32_t delayMillisec)
{
   printf("loop 1 returned: %s\n", (co_await testco("loop 1", delayMillisec)).c_str());
   printf("loop 2 returned: %s\n", (co_await testco("loop 2", delayMillisec)).c_str());
   printf("testco2 finished\n");
}

clintrinsics::Task<> testDb()
{
   auto db = co_await clintrinsics::openDb("foo");
   printf(">> database handle: %p\n", db.handle);
   auto trx = db.createTransaction(true);
   printf(">> trx handle: %p\n", trx.handle);
   co_await trx.setKV("abcd", "efgh");
   co_await trx.setKV("ijkl", "mnop");

    for
       co_await(auto x : co_await trx.everything()) printf("... blob handle %p\n", x.handle);

    trx.commit();
    db.close();
}

clintrinsics::Task<> testNet()
{
   clintrinsics::Connection myConnection{"ws", "localhost", 9125};
   myConnection.onOpen = []() { printf("connection opened!\n"); };
   myConnection.onMessage = [](clintrinsics::ExternalBytes data) {
      printf("received bytes handle: %p -- size: %d\n", data.handle,
             (int)data.toUint8Vector().size());
   };
   myConnection.onClose = [](uint32_t code) { printf("connection closed code: %d\n", code); };
   myConnection.onError = []() { printf("client connection has failed!\n"); };
   co_await myConnection.connect();

   printf(">> connection handle: %p\n", myConnection.handle);
   co_await myConnection.sendMessage("hey there from CLARION wasm! :)\n");  // todo: make send async
   printf(">> message sent!\n");

   co_await clintrinsics::later(5000);  // waits for some messages exchanges before closing it

   myConnection.close();
   printf(">> connection closed!\n");
}

clintrinsics::Task<> testCrypto()
{
   printf("testing hashing...\n");
   std::string message = "hey ho lets go!";
   auto sha256 = co_await clintrinsics::hash256(message.c_str(), message.size());
   printf(">> sha256 message size: %d - %s \n", (int)sha256.size(), sha256.toString().c_str());

   printf("testing synchronous hashing...\n");
   std::string messageSync = "hey ho lets go! sync";
   auto sha256Sync = clintrinsics::hash256Sync(messageSync.c_str(), messageSync.size());
   printf(">> sha256Sync message size: %d - %s \n", (int)sha256Sync.size(),
          sha256Sync.toString().c_str());

   printf("creating new k1 public key...\n");
   auto k1PubKey2 = co_await clintrinsics::createKey<clintrinsics::EccCurve::k1>();
   printf("k1 pubKey third byte >> %c\n", k1PubKey2[2]);

   printf("creating new r1 public key...\n");
   auto r1PubKey2 = co_await clintrinsics::createKey<clintrinsics::EccCurve::r1>();
   printf("k1 pubKey third byte >> %c\n", r1PubKey2[2]);

   // todo: working on sign...
}

// todo: move to a proper node file
namespace clintrinsics
{
   struct ClarionNode
   {
      std::unordered_set<std::shared_ptr<ConnectionAcceptor>> acceptors;
      std::unordered_set<std::shared_ptr<Connection>> connections;

      void addConnection(std::shared_ptr<Connection> connection)
      {
         connections.emplace(connection);
      }

      void removeConnection(std::shared_ptr<Connection> connection)
      {
         connections.erase(connection);
      }

      void status()
      {
         printf(">>> Acceptors %d / Connections: %d\n", (int)acceptors.size(),
                (int)connections.size());
      }
   };
}  // namespace clintrinsics

static clintrinsics::ClarionNode node;
static std::shared_ptr<clintrinsics::ConnectionAcceptor> globalAcceptor;

void setupGlobalAcceptor()
{
   globalAcceptor = std::make_shared<clintrinsics::ConnectionAcceptor>(9125, "ws");
   node.acceptors.emplace(globalAcceptor);

   globalAcceptor->onConnection = [](std::shared_ptr<clintrinsics::Connection> connection) {
      printf("conn(%d/%p) >>> new incoming connection: ([%s] remote: %s:%d / local: %s:%d)\n",
             globalAcceptor->port, connection->handle, connection->protocol.c_str(),
             connection->remoteAddress.c_str(), connection->remotePort,
             connection->localAddress.c_str(), connection->localPort);
      connection->onMessage = [connection](clintrinsics::ExternalBytes data) {
         auto dataBytes = data.toUint8Vector();
         printf("conn(%d/%p) >>> received bytes handle: %p -- size: %d\n", globalAcceptor->port,
                connection->handle, data.handle, (int)dataBytes.size());
         connection->sendMessage(dataBytes.data(), dataBytes.size()).run();
      };
      connection->onClose = [connection](uint32_t code) {
         printf("conn(%d/%p) >>> connection closed code: %d\n", globalAcceptor->port,
                connection->handle, code);
         node.removeConnection(connection);
      };
      connection->onError = [connection]() {
         printf("conn(%d/%p) >>> connection failed with error!\n", globalAcceptor->port,
                connection->handle);
      };
      connection->setup();
      connection->sendMessage("welcome to a world with Clarity").run();

      printf("conn(%d/%p) >>> setup done!\n", globalAcceptor->port, connection->handle);

      node.addConnection(connection);
   };
   globalAcceptor->listen();
   printf("global acceptor handle >> %p\n", globalAcceptor->handle);
}

[[clang::export_name("initServer")]] void initServer()
{
   printf("initializing clariond server...\n");
   setupGlobalAcceptor();
   printf("clariond initialization server returned\n");
}

[[clang::export_name("status")]] void status()
{
   node.status();
}

[[clang::export_name("test")]] void test()
{
   // testco("delay 1s", 1000).start();
   // testco("delay 2s", 2000).start();
   // testco("delay 3s", 3000).start();
   // testco2(200).start();
   // testco2(250).start();
   // testDb().start();
   // testNet().start();
   testCrypto().start();
}

int main()
{
   printf("starting coroutines...\n");
   test();
   printf("main returned\n");
}
