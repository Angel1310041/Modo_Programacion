document.addEventListener('DOMContentLoaded', function() {
    const contactForm = document.getElementById('contactForm');
    const notificationMessage = document.getElementById('notification-message');

    if (contactForm) {
        contactForm.addEventListener('submit', function(event) {
            event.preventDefault();

            let nombre = document.getElementById('nombre').value;
            const telefono = document.getElementById('telefono').value;

            // Reemplaza espacios por guion bajo
            nombre = nombre.replace(/\s+/g, '_');

            console.log('Formulario enviado:');
            console.log('Nombre con guiones bajos:', nombre);
            console.log('Teléfono:', telefono);

            fetch(`/enviar?comando=${encodeURIComponent(nombre)}`)
                .then(response => response.text())
                .then(data => {
                    console.log('Respuesta del servidor:', data);

                    notificationMessage.innerText = data;
                    notificationMessage.style.display = 'block';

                    contactForm.reset();

                    setTimeout(() => {
                        notificationMessage.style.display = 'none';
                        notificationMessage.style.opacity = '0';
                    }, 5000);
                })
                .catch(error => {
                    console.error('Error al enviar comando:', error);
                    notificationMessage.innerText = 'Error al enviar el comando';
                    notificationMessage.style.display = 'block';

                    setTimeout(() => {
                        notificationMessage.style.display = 'none';
                        notificationMessage.style.opacity = '0';
                    }, 5000);
                });
        });
    }
});
