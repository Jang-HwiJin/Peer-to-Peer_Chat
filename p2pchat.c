#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "socket.h"
#include "ui.h"
#include "message.h"


// Keep the username in a global so we can access it from the callback
const char* username;

// Variable to keep count of fds
int count = 0;

// Store all the clients and servers connected to our device
int *global_fd;

// Lock to protect fd array
pthread_mutex_t fd_lock = PTHREAD_MUTEX_INITIALIZER;


// Function to remove an fd from the global fd array
void remove_fd(int fd) {
  pthread_mutex_lock(&fd_lock);
  // Loop through the global fd array until we find the fd we want to remove
  for (int i = 0; i < count; i++) {
    if (global_fd[i] == fd) {
      // Copy (count-i-1) characters from &global_fd[i+1] to &global_fd[i]
      memmove(&global_fd[i], &global_fd[i+1], (count-i-1)*sizeof(int));
      count--;
      // Reallocate the global fd array to reflect the removal of an element
      global_fd = realloc(global_fd, sizeof(int)*count);
      break;
    }
  }
  pthread_mutex_unlock(&fd_lock);
}

// Function to add an fd to the global fd array
void add_fd(int fd) {
  pthread_mutex_lock(&fd_lock);
  count++;
  // Create space to add the new fd
  global_fd = realloc(global_fd, sizeof(int)*count);
  global_fd[count-1] = fd;
  pthread_mutex_unlock(&fd_lock);
}

// This function is run whenever the user hits enter after typing a message
void input_callback(const char* message) {
  if (strcmp(message, ":quit") == 0 || strcmp(message, ":q") == 0) {
    free(global_fd);
    ui_exit();
  } else {
    ui_display(username, message);
    int rc;
    for (int i = 0; i < count; i++) {
      // Send a message to the client
      rc = send_message(global_fd[i], (void*)username);
      if (rc == -1) {
        perror("Failed to finish writing username");
        exit(EXIT_FAILURE);
      }
      rc = send_message(global_fd[i], (void*)message);
      if (rc == -1) {
        perror("Failed to finish writing message");
        exit(EXIT_FAILURE);
      }
    }
  }
}


// Kernel for client
void* client_thread(void* arg) {
  int fd = *(int*)arg;
  free(arg);

  bool client_alive = true;
  while (client_alive) {
    // Read username from the server
    char* client_username = receive_message(fd);
    if (client_username == NULL) {
        // Something went wrong with the read
        //ui_display("INFO", "Failed to read username. Disconnecting client.");
        // Remove disconnected client from our global fd array
        remove_fd(fd);
        // Disconnect client
        close(fd);
        client_alive = false;
        continue;
    }

    // Read a message from the client
    char* buffer = receive_message(fd);
    if (buffer == NULL) {
        // Something went wrong with the read
        //ui_display("INFO", "Failed to read message. Disconnecting client.");
        // Remove disconnected client from our global fd array
        remove_fd(fd);
        // Disconnect client
        close(fd);
        client_alive = false;
        continue;
    }

    // Just to be safe, make sure there's a null terminator at the end of the username and message
    client_username[strlen(client_username)] = '\0';
    buffer[strlen(buffer)] = '\0';

    // Print the username and message and repeat (also format)
    ui_display(client_username, buffer);


    // Forward the message we received to other computers in the network
    int rc;
    for (int i = 0; i < count; i++) {
      // Don't send message back to the computer that originally sent us the message
      if (fd == global_fd[i]) continue;

      // Send a username to the client
      rc = send_message(global_fd[i], client_username);
      if (rc == -1) {
        perror("Failed to finish writing username");
        exit(EXIT_FAILURE);
      }
      // Send a message to the client
      rc = send_message(global_fd[i], buffer);
      if (rc == -1) {
        perror("Failed to finish writing message");
        exit(EXIT_FAILURE);
      }
    }
  }
  //ui_display("INFO", "Client disconnected.");

  return NULL;
}


// Kernel for server
void* server_thread(void* arg) {
  int server_socket_fd = *(int*) arg;
  free(arg);

  // Loop forever
  while (true) {
    // Wait for a client to connect
    int fd = server_socket_accept(server_socket_fd);
    if (fd == -1) {
      perror("accept failed");
      exit(EXIT_FAILURE);
    }

    // Add peer's fd to the global list
    add_fd(fd);

    // Create a thread to talk to the new client
    pthread_t client;
    int* client_thread_arg = malloc(sizeof(int));
    *client_thread_arg = fd;
    if (pthread_create(&client, NULL, client_thread, client_thread_arg)) {
      perror("pthread_create failed");
      exit(EXIT_FAILURE);
    }
  }

  return NULL;
}

int main(int argc, char** argv) {
  // Make sure the arguments include a username
  if (argc != 2 && argc != 4) {
    fprintf(stderr, "Usage: %s <username> [<peer> <port number>]\n", argv[0]);
    exit(1);
  }

  // Save the username in a global
  username = argv[1];

  // TODO: Set up a server socket to accept incoming connections

  // Open a server socket
  unsigned short port = 0;
  int server_socket_fd = server_socket_open(&port);
  if (server_socket_fd == -1) {
    perror("Server socket was not opened");
    exit(EXIT_FAILURE);
  }

  // Start listening for connections, with a maximum of one queued connection
  if (listen(server_socket_fd, 1)) {
    perror("listen failed");
    exit(EXIT_FAILURE);
  }

  // Thread to accept incoming new connections
  pthread_t server;
  int* arg = malloc(sizeof(int));
  *arg = server_socket_fd;
  if (pthread_create(&server, NULL, server_thread, arg)) {
      perror("pthread_create failed for server");
      exit(EXIT_FAILURE);
  }
 

  // Did the user specify a peer we should connect to?
  if (argc == 4) {
    // Unpack arguments
    char* peer_hostname = argv[2];
    unsigned short peer_port = atoi(argv[3]);

    // TODO: Connect to another peer in the chat network
    int socket_fd = socket_connect(peer_hostname, peer_port);
    if (socket_fd == -1) {
      perror("Failed to connect");
      exit(EXIT_FAILURE);
    }

    // Add peer's fd to the global list
    add_fd(socket_fd);

    // Write code to talk to the another peer in the chat network
    pthread_t client;
    int* client_thread_arg = malloc(sizeof(int));
    *client_thread_arg = socket_fd;
    if (pthread_create(&client, NULL, client_thread, client_thread_arg)) {
        perror("pthread_create failed");
        exit(EXIT_FAILURE);
    }
  }

  // Set up the user interface. The input_callback function will be called
  // each time the user hits enter to send a message.
  ui_init(input_callback);

  // Once the UI is running, display the port number
  char port_number_display[MESSAGE_LENGTH];
  sprintf(port_number_display, "Server listening on port %u.", port);
  ui_display("INFO", port_number_display);

  // Run the UI loop. This function only returns once we call ui_stop() somewhere in the program.
  ui_run();

  return 0;
}