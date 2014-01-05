#pragma once

#ifndef HTTP_CLIENT_HPP_VIA_HTTPLIB_
#define HTTP_CLIENT_HPP_VIA_HTTPLIB_
//////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2013-2014 Ken Barker
// (ken dot barker at via-technology dot co dot uk)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////
/// @file http_client.hpp
/// @brief Just contains the http_client template class.
//////////////////////////////////////////////////////////////////////////////
#include "via/http/request.hpp"
#include "via/http/response.hpp"
#include "via/comms/connection.hpp"
#include <boost/shared_ptr.hpp>
#include <boost/signals2.hpp>
#include <deque>
#include <iostream>
#include <boost/bind.hpp>

namespace via
{
  ////////////////////////////////////////////////////////////////////////////
  /// @class http_client
  /// An HTTP client.
  /// @param SocketAdaptor the type of socket to use, tcp or ssl
  /// @param Container the type of container to use
  /// @param use_strand if true use an asio::strand to wrap the handlers
  ////////////////////////////////////////////////////////////////////////////
  template <typename SocketAdaptor, typename Container = std::vector<char>,
            bool use_strand = false>
  class http_client
  {
  public:
    /// The underlying connection, TCP or SSL.
    typedef comms::connection<SocketAdaptor, Container, use_strand>
                                                              connection_type;

    /// A shared pointer to this type.
    typedef typename boost::shared_ptr<http_client<SocketAdaptor, Container,
                                                 use_strand> > shared_pointer;

    /// The template requires a typename to access the iterator.
    typedef typename Container::const_iterator Container_const_iterator;

    /// The signal sent when a response is received.
    typedef boost::signals2::signal<void (http::rx_response const&,
                                      Container const&)> http_response_signal;

    /// The slot type associated with a response received signal.
    typedef typename http_response_signal::slot_type http_response_signal_slot;

    /// The chunk type
    typedef typename http::rx_chunk<Container> chunk_type;

    /// The signal sent when a chunk is received.
    typedef boost::signals2::signal<void (chunk_type const&,
                                          Container const&)> http_chunk_signal;

    /// The slot type associated with a chunk received signal.
    typedef typename http_chunk_signal::slot_type http_chunk_signal_slot;

    /// The signal sent when a socket is disconnected.
    typedef boost::signals2::signal<void (void)> http_disconnected_signal;

    /// The slot type associated with a disconnected signal.
    typedef typename http_disconnected_signal::slot_type
                                                http_disconnected_signal_slot;

  private:

    boost::shared_ptr<connection_type> connection_; ///< the comms connection
    http::response_receiver<Container> rx_;       ///< the response receiver
    http_response_signal http_response_signal_;   ///< the response callback function
    http_chunk_signal http_chunk_signal_;         ///< the response chunk callback function
    http_disconnected_signal http_disconnected_signal_;
    std::string host_name_;                       ///< the name of the host

    /// Send a packet on the connection.
    /// @param packet the data packet to send.
    void send(Container const& packet)
    {
      rx_.clear();
      connection_->send_data(packet);
    }

#if defined(BOOST_ASIO_HAS_MOVE)
    /// Send a packet on the connection.
    /// @param packet the data packet to send.
    void send(Container&& packet)
    {
      rx_.clear();
      connection_->send_data(packet);
    }
#endif  // BOOST_ASIO_HAS_MOVE

    /// Constructor.
    /// @param io_service the asio io_service to use.
    explicit http_client(boost::asio::io_service& io_service) :
      connection_(connection_type::create(io_service)),
      rx_(),
      http_response_signal_(),
      http_chunk_signal_(),
      http_disconnected_signal_(),
      host_name_()
    {
      connection_->set_event_callback
          (boost::bind(&http_client::event_handler, this, _1, _2));
      connection_->set_error_callback
          (boost::bind(&http_client::error_handler, this,
                       boost::asio::placeholders::error, _2));
    }

  public:

    /// @fn create
    /// The factory function to create connections.
    /// @param io_service the boost asio io_service used by the underlying
    /// connection.
    static shared_pointer create(boost::asio::io_service& io_service)
    { return shared_pointer(new http_client(io_service)); }

    /// Connect the response received slot.
    /// @param slot the slot for the response received signal.
    void response_received_event(http_response_signal_slot const& slot)
    { http_response_signal_.connect(slot); }

    /// Connect the chunk received slot.
    /// @param slot the slot for the chunk received signal.
    void chunk_received_event(http_chunk_signal_slot const& slot)
    { http_chunk_signal_.connect(slot); }

    /// Connect the disconnected slot.
    /// @param slot the slot for the disconnected signal.
    void disconnected_event(http_disconnected_signal_slot const& slot)
    { http_disconnected_signal_.connect(slot); }

    /// Connect to the given host name and port.
    /// @param host_name the host to connect to.
    /// @param port_name the port to connect to.
    /// @return true if resolved, false otherwise.
    bool connect(const std::string& host_name, std::string port_name = "http")
    {
      host_name_ = host_name;
      if ((port_name != "http") && (port_name != "https"))
      {
        host_name_ += ":";
        host_name_ += port_name;
      }

      return connection_->connect(host_name.c_str(), port_name.c_str());
    }

    /// Accessor for the HTTP response header.
    /// @return a constant reference to an rx_response.
    http::rx_response const& response() const
    { return rx_.response(); }

    /// Accessor for the beginning of the body.
    /// @return a constant iterator to the beginning of the body.
    Container_const_iterator body_begin() const
    { return rx_.body().begin(); }

    /// Accessor for the end of the body.
    /// @return a constant iterator to the end of the body.
    Container_const_iterator body_end() const
    { return rx_.body().end(); }

    /// Receive data on the underlying connection.
    void receive_handler()
    {
      // attempt to read the data
      Container data(connection_->rx_buffer());
      http::receiver_parsing_state rx_state(rx_.receive(data.begin(),
                                                        data.end()));
      switch (rx_state)
      {
      case http::RX_VALID:
        http_response_signal_(rx_.response(), rx_.body());
        return;

      case http::RX_CHUNK:
        http_chunk_signal_(rx_.chunk(), rx_.chunk().data());
        return;

      case http::RX_INVALID:
        break;

      default:
        break;
      }
    }

    /// Send an HTTP request without a body.
    /// @param request the request to send.
    void send(http::tx_request& request)
    {
      request.add_header(http::header_field::HOST, host_name_);
      std::string http_header(request.message());
      Container tx_message(http_header.begin(), http_header.end());
      send(tx_message);
    }

#if defined(BOOST_ASIO_HAS_MOVE)
    /// Send an HTTP request without a body.
    /// @param request the request to send.
    void send(http::tx_request&& request)
    {
      request.add_header(http::header_field::HOST, host_name_);
      std::string http_header(request.message());
      Container tx_message(http_header.begin(), http_header.end());
      send(tx_message);
    }
#endif // BOOST_ASIO_HAS_MOVE

    /// Send an HTTP request with a body.
    /// @param request the request to send.
    /// @param body the body to send
    void send(http::tx_request& request, Container const& body)
    {
      request.add_header(http::header_field::HOST, host_name_);
      std::string http_header(request.message());

      Container tx_message(body);
      tx_message.insert(tx_message.begin(),
                        http_header.begin(), http_header.end());
      send(tx_message);
    }

#if defined(BOOST_ASIO_HAS_MOVE)
    /// Send an HTTP request with a body.
    /// @param request the request to send.
    /// @param body the body to send
    void send(http::tx_request&& request, Container&& body)
    {
      request.add_header(http::header_field::HOST, host_name_);
      std::string http_header(request.message(body.size()));

      body.insert(body.begin(),
                  http_header.begin(), http_header.end());
      send(body);
    }
#endif // BOOST_ASIO_HAS_MOVE

    /// Send an HTTP request with a body.
    /// @param request the request to send.
    /// @param begin a constant iterator to the beginning of the body to send.
    /// @param end a constant iterator to the end of the body to send.
    template<typename ForwardIterator1, typename ForwardIterator2>
    bool send(http::tx_request& request,
              ForwardIterator1 begin, ForwardIterator2 end)
    {
      request.add_header(http::header_field::HOST, host_name_);
      size_t size(end - begin);
      std::string http_header(request.message(size));

      Container tx_message;
      tx_message.reserve(http_header.size() + size);
      tx_message.assign(http_header.begin(), http_header.end());
      tx_message.insert(tx_message.end(), begin, end);
      return send(tx_message);
    }

    /// Send an HTTP body chunk.
    /// @param chunk the body chunk to send
    /// @param extension the (optional) chunk extension.
    void send_chunk(Container const& chunk, std::string extension = "")
    {
      size_t size(chunk.size());
      http::chunk_header chunk_header(size, extension);
      std::string chunk_string(chunk_header.to_string());

      Container tx_message(chunk);
      tx_message.insert(tx_message.begin(),
                        chunk_string.begin(), chunk_string.end());
      tx_message.insert(tx_message.end(),
                        http::CRLF.begin(), http::CRLF.end());
      send(tx_message);
    }

#if defined(BOOST_ASIO_HAS_MOVE)
    /// Send an HTTP body chunk.
    /// @param chunk the body chunk to send
    /// @param extension the (optional) chunk extension.
    void send_chunk(Container&& chunk, std::string extension = "")
    {
      size_t size(chunk.size());
      http::chunk_header chunk_header(size, extension);
      std::string chunk_string(chunk_header.to_string());

      chunk.insert(chunk.begin(),
                   chunk_string.begin(), chunk_string.end());
      chunk.insert(chunk.end(),
                   http::CRLF.begin(), http::CRLF.end());
      send(chunk);
    }
#endif // BOOST_ASIO_HAS_MOVE

    /// Send an HTTP body chunk.
    /// @param begin a constant iterator to the beginning of the chunk to send.
    /// @param end a constant iterator to the end of the chunk to send.
    /// @param extension the (optional) chunk extension.
    template<typename ForwardIterator1, typename ForwardIterator2>
    void send_chunk(ForwardIterator1 begin, ForwardIterator2 end,
                    std::string extension = "")
    {
      size_t size(end - begin);
      http::chunk_header chunk_header(size, extension);
      std::string chunk_string(chunk_header.to_string());

      Container tx_message;
      tx_message.reserve(chunk_string.size() + size);
      tx_message.assign(chunk_string.begin(), chunk_string.end());
      tx_message.insert(tx_message.end(), begin, end);
      tx_message.insert(tx_message.end(),
                        http::CRLF.begin(), http::CRLF.end());
      send(tx_message);
    }

    /// Send the last HTTP chunk for a request.
    /// @param extension the (optional) chunk extension.
    /// @param trailer_string the (optional) chunk trailers.
    void last_chunk(std::string extension = "",
                    std::string trailer_string = "")
    {
      http::last_chunk last_chunk(extension, trailer_string);
      std::string chunk_string(last_chunk.message());

      Container tx_message(chunk_string.begin(), chunk_string.end());
      send(tx_message);
    }

    /// Send a message body on the connection.
    /// @param body the body to send.
    void send_body(Container const& body)
    { send(body); }

#if defined(BOOST_ASIO_HAS_MOVE)
    /// Send a message body on the connection.
    /// @param body the body to send.
    void send_body(Container&& body)
    { send(body); }
#endif  // BOOST_ASIO_HAS_MOVE

    /// Disconnect the underlying connection.
    void disconnect()
    { connection_.lock()->disconnect(); }

    /// Receive an event from the underlying comms connection.
    /// @param event the type of event.
    /// @param connection a weak ponter to the underlying comms connection.
    void event_handler(int event,
                       typename connection_type::weak_pointer connection)
    {
      switch(event)
      {
      case via::comms::RECEIVED:
        receive_handler();
        break;
      case via::comms::DISCONNECTED:
        http_disconnected_signal_();
        break;
      default:
        break;
      }
    }

    /// Receive an error from the underlying comms connection.
    /// @param error the boost error_code.
    /// @param connection a weak ponter to the underlying comms connection.
    void error_handler(const boost::system::error_code &error,
                       typename connection_type::weak_pointer connection)
    {
      std::cerr << "error_handler" << std::endl;
      std::cerr << error <<  std::endl;
    }
  };
}

#endif
