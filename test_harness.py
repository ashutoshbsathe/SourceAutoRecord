import grpc
import harness_pb2
import harness_pb2_grpc

def run():
    # Attempt to connect to the local SAR gRPC server
    with grpc.insecure_channel('localhost:50051') as channel:
        stub = harness_pb2_grpc.Portal2HarnessStub(channel)
        
        print("Sending InitialHandshake...")
        try:
            response = stub.InitialHandshake(harness_pb2.HandshakeRequest(
                client_version="1.0.0",
                client_id="test_client"
            ))
            print("Handshake Successful!")
            print(f"Game Version: {response.game_version}")
            print(f"Current Map: {response.map_name}")
        except grpc.RpcError as e:
            print(f"RPC failed: {e.code()} - {e.details()}")

if __name__ == '__main__':
    run()
